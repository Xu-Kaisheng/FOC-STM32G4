/** @file perf_print.c
 *  @brief 每秒一次的性能日志格式化输出
 *
 * 打印策略属于应用层：BSP 只负责把字节送出去，不再认识 SIXSTEP/CURRENT
 * 这类业务分类。调用 app_log_write 是向下调用，不构成依赖环。
 */
#include "perf.h"
#include "app_log.h"
#include "main.h"

#define PERF_CYCLES_PER_US_DIVISOR 1000000u
#define PERF_PERCENT_SCALE        100.0
#define PERF_MS_PER_SECOND        1000.0

/* ---------------- internal ---------------- */

/** @brief 输出指定控制任务；没有样本时不将零耗时伪装成已运行。 */
static void print_control(const app_perf_snapshot_t *sample, app_perf_control_t id, const char *name)
{
    const app_perf_control_stats_t *stats = &sample->control[id];
    double cycles_per_us = (double)sample->clock_hz / (double)PERF_CYCLES_PER_US_DIVISOR;
    if (stats->count == 0u)
    {
        app_log_write("[PERF] %s=OFF (no samples)\r\n", name);
        return;
    }
    app_log_write("[PERF] %s n=%u avg=%.2fus max=%.2fus share=%.2f%%\r\n", name,
                    (unsigned)stats->count, (double)stats->total_cycles / stats->count / cycles_per_us,
                    (double)stats->max_cycles / cycles_per_us,
                    (double)stats->total_cycles * PERF_PERCENT_SCALE / sample->elapsed_cycles);
    if (id == APP_PERF_CURRENT)
    {
        app_log_write("[PERF] CURRENT budget=%uus peak=%.2f%% over=%u\r\n",
                        (unsigned)APP_PERF_CURRENT_PERIOD_US,
                        (double)stats->max_cycles * PERF_PERCENT_SCALE / cycles_per_us / APP_PERF_CURRENT_PERIOD_US,
                        (unsigned)stats->overruns);
    }
}

/* ---------------- task ---------------- */

/** @brief 每秒在主循环格式化日志，统计输出自身计入下一窗口有效工作。 */
void app_perf_task(void)
{
    app_perf_snapshot_t sample;
    uint32_t now = HAL_GetTick();

    if (app_perf_check_report(now) == 0u)
    {
        return;
    }
    if (app_perf_get_snapshot(&sample) == 0u)
    {
        app_log_write("[PERF] unavailable (DWT/clock/window)\r\n");
        return;
    }
    app_perf_begin_work();
    app_log_write("[PERF] CPU work=%.2f%% busy=%.3fms poll=%.3fms irq=%.3fms win=%.3fms\r\n",
                    (double)sample.work_cycles * PERF_PERCENT_SCALE / sample.elapsed_cycles,
                    (double)sample.work_cycles * PERF_MS_PER_SECOND / sample.clock_hz,
                    (double)(sample.elapsed_cycles - sample.work_cycles) * PERF_MS_PER_SECOND / sample.clock_hz,
                    (double)sample.irq_cycles * PERF_MS_PER_SECOND / sample.clock_hz,
                    (double)sample.elapsed_cycles * PERF_MS_PER_SECOND / sample.clock_hz);
    print_control(&sample, APP_PERF_SIXSTEP, "SIXSTEP");
    print_control(&sample, APP_PERF_CURRENT, "CURRENT");
    app_perf_end_work();
}
