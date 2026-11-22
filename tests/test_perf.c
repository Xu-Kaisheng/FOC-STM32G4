/* 通过可控周期时钟验证时间并集、回绕、预算及真实日志，而非模拟统计函数。 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "perf.h"
#include "app_log.h"

#define TEST_HZ 170000000u
#define TEST_CYCLES_PER_US 170u
static uint32_t s_tick;
static char s_log[2048];

/** @brief 测试毫秒时间与 DWT 分别推进，以覆盖回绕和停表路径。 */
uint32_t HAL_GetTick(void) { return s_tick; }

/** @brief 捕获实际格式化输出，同时验证每条日志不会被串口缓冲截断。 */
void app_log_write(const char *fmt, ...)
{
    va_list args;
    char line[256];
    int32_t size;
    va_start(args, fmt);
    size = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    assert(size > 0 && size < 128);
    assert(strlen(s_log) + (uint32_t)size < sizeof(s_log));
    strcat_s(s_log, sizeof(s_log), line);
}

/** @brief 使用指定时钟起点启动，计时不包含初始化中的 NOP。 */
static uint32_t start_clock(uint32_t start)
{
    SystemCoreClock = TEST_HZ;
    test_dwt.CYCCNT = start;
    test_primask = 0u;
    test_dwt_running = 1u;
    s_tick = 0u;
    s_log[0] = '\0';
    app_perf_init();
    assert(test_primask == 0u);
    assert((test_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u);
    return test_dwt.CYCCNT;
}

/** @brief 验证空轮询不算工作，嵌套工作/中断按并集计算。 */
static void test_union(void)
{
    app_perf_snapshot_t snap;
    uint32_t base = start_clock(0u);
    test_dwt.CYCCNT = base + 10u;
    app_perf_begin_work();
    test_dwt.CYCCNT = base + 20u;
    app_perf_enter_irq();
    test_dwt.CYCCNT = base + 30u;
    app_perf_enter_irq();
    test_dwt.CYCCNT = base + 40u;
    app_perf_exit_irq();
    test_dwt.CYCCNT = base + 50u;
    app_perf_exit_irq();
    test_dwt.CYCCNT = base + 60u;
    app_perf_end_work();
    test_dwt.CYCCNT = base + 70u;
    app_perf_enter_irq();
    test_dwt.CYCCNT = base + 80u;
    app_perf_exit_irq();
    test_dwt.CYCCNT = base + 100u;
    assert(app_perf_get_snapshot(&snap) == 1u);
    assert(snap.elapsed_cycles == 100u && snap.work_cycles == 60u && snap.irq_cycles == 40u);
    assert(snap.control[APP_PERF_CURRENT].count == 0u);
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snap) == 1u && snap.work_cycles == 0u && snap.irq_cycles == 0u);
    // 快照跨越尚未结束的前台任务，下一窗口从快照位置续算。
    app_perf_begin_work();
    test_dwt.CYCCNT += 30u;
    test_primask = 1u;
    assert(app_perf_get_snapshot(&snap) == 1u && snap.work_cycles == 30u);
    assert(test_primask == 1u);
    test_primask = 0u;
    test_dwt.CYCCNT += 20u;
    app_perf_end_work();
    test_dwt.CYCCNT += 50u;
    assert(app_perf_get_snapshot(&snap) == 1u && snap.work_cycles == 20u && snap.elapsed_cycles == 70u);
}

/** @brief 验证一次自然回绕、50 us 临界值与抢占响应时间。 */
static void test_control(void)
{
    app_perf_snapshot_t snap;
    uint32_t base = start_clock(UINT32_MAX - 100u);
    uint32_t mark = app_perf_begin_control();
    test_dwt.CYCCNT += 10u * TEST_CYCLES_PER_US;
    app_perf_enter_irq();
    test_dwt.CYCCNT += 5u * TEST_CYCLES_PER_US;
    app_perf_exit_irq();
    test_dwt.CYCCNT += 5u * TEST_CYCLES_PER_US;
    app_perf_end_control(APP_PERF_CURRENT, mark);
    mark = app_perf_begin_control();
    test_dwt.CYCCNT += 50u * TEST_CYCLES_PER_US;
    app_perf_end_control(APP_PERF_CURRENT, mark);
    mark = app_perf_begin_control();
    test_dwt.CYCCNT += 100u * TEST_CYCLES_PER_US;
    app_perf_end_control(APP_PERF_SIXSTEP, mark);
    test_dwt.CYCCNT = base + TEST_HZ;
    s_tick = 1000u;
    assert(app_perf_get_snapshot(&snap) == 1u);
    assert(snap.elapsed_cycles == TEST_HZ);
    assert(snap.work_cycles == 170u * TEST_CYCLES_PER_US);
    assert(snap.irq_cycles == 5u * TEST_CYCLES_PER_US);
    assert(snap.control[APP_PERF_CURRENT].count == 2u);
    assert(snap.control[APP_PERF_CURRENT].total_cycles == 70u * TEST_CYCLES_PER_US);
    assert(snap.control[APP_PERF_CURRENT].max_cycles == 50u * TEST_CYCLES_PER_US);
    assert(snap.control[APP_PERF_CURRENT].overruns == 1u);
    assert(snap.control[APP_PERF_SIXSTEP].overruns == 0u);
}

/** @brief 验证无样本提示、比例换算、日志长度及计数器不可用路径。 */
static void test_reporting(void)
{
    uint32_t base = start_clock(0u);
    uint32_t mark;
    app_perf_begin_work();
    test_dwt.CYCCNT += TEST_HZ / 4u;
    app_perf_end_work();
    test_dwt.CYCCNT = base + TEST_HZ;
    s_tick = 1000u;
    app_perf_task();
    assert(strstr(s_log, "work=25.00% busy=250.000ms poll=750.000ms") != NULL);
    assert(strstr(s_log, "CURRENT=OFF") != NULL && strstr(s_log, "SIXSTEP=OFF") != NULL);
    s_log[0] = '\0';
    mark = app_perf_begin_control();
    test_dwt.CYCCNT += 50u * TEST_CYCLES_PER_US;
    app_perf_end_control(APP_PERF_CURRENT, mark);
    test_dwt.CYCCNT = base + 2u * TEST_HZ;
    s_tick = 2000u;
    app_perf_task();
    assert(strstr(s_log, "avg=50.00us max=50.00us") != NULL);
    assert(strstr(s_log, "budget=50us peak=100.00% over=1") != NULL);
    s_log[0] = '\0';
    test_dwt_running = 0u;
    app_perf_init();
    s_tick += 1000u;
    app_perf_task();
    assert(strstr(s_log, "unavailable") != NULL && strstr(s_log, "work=") == NULL);
}

/** @brief 超长窗口和改频均不能发布貌似正常的 CPU 负载。 */
static void test_invalid_window(void)
{
    app_perf_snapshot_t snap = {0};
    (void)start_clock(0u);
    test_dwt.CYCCNT += 100u;
    s_tick = 10001u;
    assert(app_perf_get_snapshot(&snap) == 0u && snap.clock_hz == 0u);
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snap) == 1u);
    SystemCoreClock /= 2u;
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snap) == 0u);
    assert(app_perf_get_snapshot(NULL) == 0u);
}

/** @brief 下载器清除任一使能位时，即使窗口已有计数，也必须丢弃并在下一窗口恢复。 */
static void test_recovery(void)
{
    app_perf_snapshot_t snap;
    for (uint32_t scenario = 0u; scenario < 2u; scenario++)
    {
        uint32_t mark;
        uint32_t stopped;
        (void)start_clock(0u);
        mark = app_perf_begin_control();
        test_dwt.CYCCNT += 100u;
        app_perf_end_control(APP_PERF_SIXSTEP, mark);
        stopped = test_dwt.CYCCNT;
        if (scenario == 0u) { test_core_debug.DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk; }
        else { test_dwt.CTRL &= ~DWT_CTRL_CYCCNTENA_Msk; }
        snap.clock_hz = 123u;
        assert(app_perf_get_snapshot(&snap) == 0u && snap.clock_hz == 123u);
        s_tick = 1000u;
        app_perf_task();
        assert(strstr(s_log, "unavailable") != NULL && strstr(s_log, "CPU work=") == NULL);
        assert((test_core_debug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0u);
        assert((test_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u);
        assert(test_dwt.CYCCNT > stopped && test_primask == 0u);
        test_dwt.CYCCNT += TEST_HZ;
        s_tick += 1000u;
        assert(app_perf_get_snapshot(&snap) == 1u && snap.elapsed_cycles == TEST_HZ);
        assert(snap.work_cycles == 0u && snap.irq_cycles == 0u);
        assert(snap.control[APP_PERF_SIXSTEP].count == 0u);
    }
}

/** @brief 初始停表可以稍后恢复；真正无法计数时持续报不可用，不伪报零负载。 */
static void test_retry(void)
{
    app_perf_snapshot_t snap;
    (void)start_clock(0u);
    test_dwt_running = 0u;
    app_perf_init();
    for (uint32_t i = 0u; i < 2u; i++)
    {
        s_log[0] = '\0';
        s_tick += 1000u;
        app_perf_task();
        assert(strstr(s_log, "unavailable") != NULL && strstr(s_log, "CPU work=") == NULL);
    }
    test_dwt_running = 1u;
    s_tick += 1000u;
    app_perf_task();
    test_dwt.CYCCNT += TEST_HZ;
    s_tick += 1000u;
    assert(app_perf_get_snapshot(&snap) == 1u && snap.elapsed_cycles == TEST_HZ);
    // 就绪后突然停表，即使使能位仍在，也要重建窗口再统计。
    s_tick += 1000u;
    s_log[0] = '\0';
    app_perf_task();
    assert(strstr(s_log, "unavailable") != NULL && test_primask == 0u);
    test_dwt.CYCCNT += TEST_HZ;
    s_tick += 1000u;
    assert(app_perf_get_snapshot(&snap) == 1u && snap.elapsed_cycles == TEST_HZ);
}

/** @brief 执行实际统计模块的边界与日志回归。 */
int32_t main(void)
{
    test_union();
    test_control();
    test_reporting();
    test_invalid_window();
    test_recovery();
    test_retry();
    puts("PASS: CPU union/nested IRQ, wrap, control budget, reporting, unavailable clock");
    return 0;
}
