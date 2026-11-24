/** @file perf.c
 *  @brief 共用 Cortex-M DWT 计时，统计有效工作并集和控制任务响应时间。
 *
 * 周期计数通过 bsp/time/cycle.h 的 static inline 读取，因此本文件与
 * bsp 之间没有调用开销，也没有依赖环。
 * 为何不拆：总负载、任务独占时间与嵌套 IRQ 共用同一窗口和临界区，拆分会扩大共享接口。
 */
#include "perf.h"
#include "cycle.h"
#include "main.h"

#include <stddef.h>

#define PERF_CYCLES_PER_US_DIVISOR 1000000u
#define PERF_MAX_WINDOW_MS        10000u

static volatile uint8_t s_ready;
static volatile uint32_t s_work_depth, s_irq_depth;
static volatile uint32_t s_work_start, s_irq_start;
static volatile uint32_t s_work_cycles, s_irq_cycles;
static volatile app_perf_control_stats_t s_control[APP_PERF_CONTROL_COUNT];
static volatile uint32_t s_task_cycles[APP_PERF_TASK_COUNT], s_irq_by_source[APP_PERF_IRQ_COUNT];
static app_perf_irq_t s_irq_stack[APP_PERF_IRQ_COUNT];
static uint32_t s_irq_segment, s_task_start, s_task_irq_start;
static app_perf_task_t s_active_task = APP_PERF_TASK_COUNT;
static uint32_t s_clock_hz, s_window_start, s_window_ms;
static app_perf_snapshot_t s_latest;

/* ---------------- internal ---------------- */

/** @brief 仅在短临界区内使用；嵌套工作共用最外层起点，防止重复累计。 */
static void work_enter(uint32_t now)
{
    if (s_work_depth == 0u) { s_work_start = now; }
    s_work_depth++;
}

/** @brief 仅在短临界区内使用；最后一个工作区间退出时才累计。 */
static void work_exit(uint32_t now)
{
    if (s_work_depth != 0u)
    {
        s_work_depth--;
        if (s_work_depth == 0u) { s_work_cycles += now - s_work_start; }
    }
}

/** @brief 清理窗口内控制统计；只在禁止抢占的快照或初始化阶段调用。 */
static void clear_controls(void)
{
    uint32_t i;
    for (i = 0u; i < (uint32_t)APP_PERF_CONTROL_COUNT; i++)
    {
        s_control[i].count = 0u;
        s_control[i].total_cycles = 0u;
        s_control[i].max_cycles = 0u;
        s_control[i].overruns = 0u;
    }
}

/** @brief 同一前台任务被 IRQ 抢占的时间只计入 IRQ，跨快照时分段累计。 */
static void account_task(uint32_t now)
{
    if (s_active_task < APP_PERF_TASK_COUNT)
    {
        uint32_t irq = s_irq_cycles;
        if (s_irq_depth != 0u) { irq += now - s_irq_start; }
        s_task_cycles[s_active_task] += (now - s_task_start) - (irq - s_task_irq_start);
        s_task_start = now;
        s_task_irq_start = irq;
    }
}

/* ---------------- init ---------------- */

/** @brief 启用并验证 DWT；不依赖调试器事先开启，不重置其共享计数值。 */
void app_perf_init(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    s_latest.elapsed_cycles = 0u;
    s_ready = 0u;
    s_work_depth = s_irq_depth = 0u;
    s_work_cycles = s_irq_cycles = 0u;
    s_active_task = APP_PERF_TASK_COUNT;
    for (uint32_t i = 0u; i < APP_PERF_TASK_COUNT; i++) { s_task_cycles[i] = 0u; }
    for (uint32_t i = 0u; i < APP_PERF_IRQ_COUNT; i++) { s_irq_by_source[i] = 0u; }
    clear_controls();
    s_clock_hz = bsp_cycle_get_hz();
    if ((bsp_cycle_init() != 0u) && (s_clock_hz >= PERF_CYCLES_PER_US_DIVISOR))
    {
        s_ready = 1u;
    }
    s_window_start = bsp_cycle_get_now();
    s_window_ms = HAL_GetTick();
    __set_PRIMASK(key);
}

/* ---------------- read ---------------- */

/** @brief 到期后更新一秒快照，返回最近窗口；周期数为零表示暂无有效窗口。 */
const app_perf_snapshot_t *app_perf_update_window(void)
{
    if (((HAL_GetTick() - s_window_ms) >= APP_PERF_REPORT_MS) &&
        (app_perf_get_snapshot(&s_latest) == 0u))
    {
        app_perf_init();
    }
    return &s_latest;
}

/** @brief 快照与归零原子完成，窗口跨过 DWT 的一次自然回绕仍可计算。 */
uint8_t app_perf_get_snapshot(app_perf_snapshot_t *out)
{
    uint32_t now, ms, i, elapsed;
    uint8_t valid;
    uint32_t key;
    if (out == NULL) { return 0u; }
    key = __get_PRIMASK();
    __disable_irq();
    now = bsp_cycle_get_now();
    ms = HAL_GetTick();
    elapsed = now - s_window_start;
    // 长时间停顿或改频后丢弃窗口，避免多次回绕被显示为正常负载。
    // 下载器退出可能清除跟踪使能；即使窗口前段有计数，也不能发布部分窗口。
    valid = (uint8_t)((s_ready != 0u) && (bsp_cycle_is_enabled() != 0u) && (elapsed != 0u) &&
                     ((ms - s_window_ms) <= PERF_MAX_WINDOW_MS) &&
                     (bsp_cycle_get_hz() == s_clock_hz));
    if (s_work_depth != 0u) { s_work_cycles += now - s_work_start; s_work_start = now; }
    if (s_irq_depth != 0u) { s_irq_cycles += now - s_irq_start; s_irq_start = now; }
    if (s_irq_depth != 0u)
    {
        s_irq_by_source[s_irq_stack[s_irq_depth - 1u]] += now - s_irq_segment;
        s_irq_segment = now;
    }
    account_task(now);
    if (valid != 0u)
    {
        out->clock_hz = s_clock_hz;
        out->elapsed_cycles = elapsed;
        out->work_cycles = s_work_cycles;
        out->irq_cycles = s_irq_cycles;
        for (i = 0u; i < APP_PERF_TASK_COUNT; i++) { out->task_cycles[i] = s_task_cycles[i]; }
        for (i = 0u; i < APP_PERF_IRQ_COUNT; i++) { out->irq_by_source[i] = s_irq_by_source[i]; }
        for (i = 0u; i < (uint32_t)APP_PERF_CONTROL_COUNT; i++) { out->control[i] = s_control[i]; }
    }
    s_work_cycles = s_irq_cycles = 0u;
    s_task_irq_start = 0u;
    for (i = 0u; i < APP_PERF_TASK_COUNT; i++) { s_task_cycles[i] = 0u; }
    for (i = 0u; i < APP_PERF_IRQ_COUNT; i++) { s_irq_by_source[i] = 0u; }
    clear_controls();
    s_window_start = now;
    s_window_ms = ms;
    __set_PRIMASK(key);
    return valid;
}

/* ---------------- hook ---------------- */

/** @brief 标记前台有效工作；临界区仅更新几个整数，不进行格式化或等待。 */
void app_perf_begin_work(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u) { work_enter(bsp_cycle_get_now()); }
    __set_PRIMASK(key);
}

/** @brief 结束有效工作区间，恢复调用者原有的中断屏蔽状态。 */
void app_perf_end_work(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u) { work_exit(bsp_cycle_get_now()); }
    __set_PRIMASK(key);
}

/** @brief 任务计时仅覆盖有效分支；空轮询仍计入 POLL。 */
void app_perf_begin_task(app_perf_task_t task)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u)
    {
        s_task_start = bsp_cycle_get_now();
        s_task_irq_start = s_irq_cycles;
        s_active_task = task;
        work_enter(s_task_start);
    }
    __set_PRIMASK(key);
}

/** @brief 汇总任务独占时间，结束后恢复为无前台任务状态。 */
void app_perf_end_task(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if ((s_ready != 0u) && (s_active_task < APP_PERF_TASK_COUNT))
    {
        uint32_t now = bsp_cycle_get_now();
        account_task(now);
        s_active_task = APP_PERF_TASK_COUNT;
        work_exit(now);
    }
    __set_PRIMASK(key);
}

/** @brief 中断入口只记周期；外层 ISR 与被抢占的前台工作不重复计数。 */
void app_perf_enter_irq(app_perf_irq_t source)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u)
    {
        uint32_t now = bsp_cycle_get_now();
        work_enter(now);
        if (s_irq_depth == 0u) { s_irq_start = now; }
        else { s_irq_by_source[s_irq_stack[s_irq_depth - 1u]] += now - s_irq_segment; }
        // 正常 IRQ 来源固定，同一 IRQ 不会抢占自身，栈深不超过来源数量。
        s_irq_stack[s_irq_depth] = source;
        s_irq_segment = now;
        s_irq_depth++;
    }
    __set_PRIMASK(key);
}

/** @brief 嵌套中断按最外层区间累计；不含异常硬件入栈、退栈的全部开销。 */
void app_perf_exit_irq(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if ((s_ready != 0u) && (s_irq_depth != 0u))
    {
        uint32_t now = bsp_cycle_get_now();
        s_irq_by_source[s_irq_stack[s_irq_depth - 1u]] += now - s_irq_segment;
        s_irq_segment = now;
        s_irq_depth--;
        if (s_irq_depth == 0u) { s_irq_cycles += now - s_irq_start; }
        work_exit(now);
    }
    __set_PRIMASK(key);
}

/** @brief 控制耗时独立于 CPU 并集，保留抢占造成的响应时间增长。 */
uint32_t app_perf_begin_control(void)
{
    app_perf_begin_work();
    return bsp_cycle_get_now();
}

/** @brief 记录次数、累计、最大值与超时；ISR 路径只有整数运算。 */
void app_perf_end_control(app_perf_control_t control, uint32_t start)
{
    uint32_t elapsed = bsp_cycle_get_now() - start;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if ((s_ready != 0u) && ((uint32_t)control < (uint32_t)APP_PERF_CONTROL_COUNT))
    {
        s_control[control].count++;
        s_control[control].total_cycles += elapsed;
        if (elapsed > s_control[control].max_cycles) { s_control[control].max_cycles = elapsed; }
        if ((control == APP_PERF_CURRENT) &&
            (elapsed >= (s_clock_hz / PERF_CYCLES_PER_US_DIVISOR) * APP_PERF_CURRENT_PERIOD_US))
        {
            s_control[control].overruns++;
        }
    }
    __set_PRIMASK(key);
    app_perf_end_work();
}
