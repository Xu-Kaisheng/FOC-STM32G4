/** @file bsp_perf.c
 *  @brief 共用 Cortex-M DWT 计时，统计有效工作并集和控制任务响应时间。
 */
#include "bsp_perf.h"
#include "bsp_uart.h"
#include "main.h"
#include <stddef.h>

#define PERF_CYCLES_PER_US_DIVISOR 1000000u
#define PERF_MAX_WINDOW_MS        10000u
#define PERF_PERCENT_SCALE        100.0
#define PERF_MS_PER_SECOND        1000.0

static volatile uint8_t s_ready;
static volatile uint32_t s_work_depth, s_irq_depth;
static volatile uint32_t s_work_start, s_irq_start;
static volatile uint32_t s_work_cycles, s_irq_cycles;
static volatile bsp_perf_control_stats_t s_control[BSP_PERF_CONTROL_COUNT];
static uint32_t s_clock_hz, s_window_start, s_window_ms, s_report_ms;

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
    for (i = 0u; i < (uint32_t)BSP_PERF_CONTROL_COUNT; i++)
    {
        s_control[i].count = 0u;
        s_control[i].total_cycles = 0u;
        s_control[i].max_cycles = 0u;
        s_control[i].overruns = 0u;
    }
}

/** @brief 启用并验证 DWT；不依赖调试器事先开启，不重置其共享计数值。 */
void bsp_perf_init(void)
{
    uint32_t before;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    s_ready = 0u;
    s_work_depth = s_irq_depth = 0u;
    s_work_cycles = s_irq_cycles = 0u;
    clear_controls();
    s_clock_hz = SystemCoreClock;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    before = DWT->CYCCNT;
    __NOP(); __NOP(); __NOP(); __NOP();
    if ((DWT->CYCCNT != before) && (s_clock_hz >= PERF_CYCLES_PER_US_DIVISOR))
    {
        s_ready = 1u;
    }
    s_window_start = DWT->CYCCNT;
    s_window_ms = HAL_GetTick();
    s_report_ms = s_window_ms;
    __set_PRIMASK(key);
}

/** @brief 标记前台有效工作；临界区仅更新几个整数，不进行格式化或等待。 */
void bsp_perf_work_begin(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u) { work_enter(DWT->CYCCNT); }
    __set_PRIMASK(key);
}

/** @brief 结束有效工作区间，恢复调用者原有的中断屏蔽状态。 */
void bsp_perf_work_end(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u) { work_exit(DWT->CYCCNT); }
    __set_PRIMASK(key);
}

/** @brief 中断入口只记周期；外层 ISR 与被抢占的前台工作不重复计数。 */
void bsp_perf_irq_enter_ISR(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if (s_ready != 0u)
    {
        uint32_t now = DWT->CYCCNT;
        work_enter(now);
        if (s_irq_depth == 0u) { s_irq_start = now; }
        s_irq_depth++;
    }
    __set_PRIMASK(key);
}

/** @brief 嵌套中断按最外层区间累计；不含异常硬件入栈、退栈的全部开销。 */
void bsp_perf_irq_exit_ISR(void)
{
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if ((s_ready != 0u) && (s_irq_depth != 0u))
    {
        uint32_t now = DWT->CYCCNT;
        s_irq_depth--;
        if (s_irq_depth == 0u) { s_irq_cycles += now - s_irq_start; }
        work_exit(now);
    }
    __set_PRIMASK(key);
}

/** @brief 控制耗时独立于 CPU 并集，保留抢占造成的响应时间增长。 */
uint32_t bsp_perf_control_begin(void)
{
    bsp_perf_work_begin();
    return DWT->CYCCNT;
}

/** @brief 记录次数、累计、最大值与超时；ISR 路径只有整数运算。 */
void bsp_perf_control_end(bsp_perf_control_t control, uint32_t start)
{
    uint32_t elapsed = DWT->CYCCNT - start;
    uint32_t key = __get_PRIMASK();
    __disable_irq();
    if ((s_ready != 0u) && ((uint32_t)control < (uint32_t)BSP_PERF_CONTROL_COUNT))
    {
        s_control[control].count++;
        s_control[control].total_cycles += elapsed;
        if (elapsed > s_control[control].max_cycles) { s_control[control].max_cycles = elapsed; }
        if ((control == BSP_PERF_CURRENT) &&
            (elapsed >= (s_clock_hz / PERF_CYCLES_PER_US_DIVISOR) * BSP_PERF_CURRENT_PERIOD_US))
        {
            s_control[control].overruns++;
        }
    }
    __set_PRIMASK(key);
    bsp_perf_work_end();
}

/** @brief 快照与归零原子完成，窗口跨过 DWT 的一次自然回绕仍可计算。 */
uint8_t bsp_perf_snapshot(bsp_perf_snapshot_t *out)
{
    uint32_t now, ms, i, elapsed;
    uint8_t valid;
    uint32_t key;
    if (out == NULL) { return 0u; }
    key = __get_PRIMASK();
    __disable_irq();
    now = DWT->CYCCNT;
    ms = HAL_GetTick();
    elapsed = now - s_window_start;
    // 长时间停顿或改频后丢弃窗口，避免多次回绕被显示为正常负载。
    valid = (uint8_t)((s_ready != 0u) && (elapsed != 0u) &&
                     ((ms - s_window_ms) <= PERF_MAX_WINDOW_MS) && (SystemCoreClock == s_clock_hz));
    if (s_work_depth != 0u) { s_work_cycles += now - s_work_start; s_work_start = now; }
    if (s_irq_depth != 0u) { s_irq_cycles += now - s_irq_start; s_irq_start = now; }
    if (valid != 0u)
    {
        out->clock_hz = s_clock_hz;
        out->elapsed_cycles = elapsed;
        out->work_cycles = s_work_cycles;
        out->irq_cycles = s_irq_cycles;
        for (i = 0u; i < (uint32_t)BSP_PERF_CONTROL_COUNT; i++) { out->control[i] = s_control[i]; }
    }
    s_work_cycles = s_irq_cycles = 0u;
    clear_controls();
    s_window_start = now;
    s_window_ms = ms;
    __set_PRIMASK(key);
    return valid;
}

/** @brief 输出指定控制任务；没有样本时不将零耗时伪装成已运行。 */
static void print_control(const bsp_perf_snapshot_t *sample, bsp_perf_control_t id, const char *name)
{
    const bsp_perf_control_stats_t *stats = &sample->control[id];
    double cycles_per_us = (double)sample->clock_hz / (double)PERF_CYCLES_PER_US_DIVISOR;
    if (stats->count == 0u)
    {
        bsp_uart_printf("[PERF] %s=OFF (no samples)\r\n", name);
        return;
    }
    bsp_uart_printf("[PERF] %s n=%u avg=%.2fus max=%.2fus share=%.2f%%\r\n", name,
                    (unsigned)stats->count, (double)stats->total_cycles / stats->count / cycles_per_us,
                    (double)stats->max_cycles / cycles_per_us,
                    (double)stats->total_cycles * PERF_PERCENT_SCALE / sample->elapsed_cycles);
    if (id == BSP_PERF_CURRENT)
    {
        bsp_uart_printf("[PERF] CURRENT budget=%uus peak=%.2f%% over=%u\r\n",
                        (unsigned)BSP_PERF_CURRENT_PERIOD_US,
                        (double)stats->max_cycles * PERF_PERCENT_SCALE / cycles_per_us / BSP_PERF_CURRENT_PERIOD_US,
                        (unsigned)stats->overruns);
    }
}

/** @brief 每秒在主循环格式化日志，统计输出自身计入下一窗口有效工作。 */
void bsp_perf_task(void)
{
    bsp_perf_snapshot_t sample;
    uint32_t now = HAL_GetTick();
    if ((now - s_report_ms) < BSP_PERF_REPORT_MS) { return; }
    s_report_ms = now;
    if (bsp_perf_snapshot(&sample) == 0u)
    {
        bsp_uart_printf("[PERF] unavailable (DWT/clock/window)\r\n");
        return;
    }
    bsp_perf_work_begin();
    bsp_uart_printf("[PERF] CPU work=%.2f%% busy=%.3fms poll=%.3fms irq=%.3fms win=%.3fms\r\n",
                    (double)sample.work_cycles * PERF_PERCENT_SCALE / sample.elapsed_cycles,
                    (double)sample.work_cycles * PERF_MS_PER_SECOND / sample.clock_hz,
                    (double)(sample.elapsed_cycles - sample.work_cycles) * PERF_MS_PER_SECOND / sample.clock_hz,
                    (double)sample.irq_cycles * PERF_MS_PER_SECOND / sample.clock_hz,
                    (double)sample.elapsed_cycles * PERF_MS_PER_SECOND / sample.clock_hz);
    print_control(&sample, BSP_PERF_SIXSTEP, "SIXSTEP");
    print_control(&sample, BSP_PERF_CURRENT, "CURRENT");
    bsp_perf_work_end();
}
