/** @file perf_print.c
 *  @brief 每秒一次的性能日志格式化输出
 *
 * 打印策略属于应用层：BSP 只负责把字节送出去，不再认识 CURRENT
 * 这类业务分类。调用 app_log_write 是向下调用，不构成依赖环。
 */
#include "perf.h"
#include "app_log.h"

#define PERF_CYCLES_PER_US_DIVISOR 1000000u
#define PERF_PERCENT_SCALE        100.0

/* ---------------- internal ---------------- */

/** @brief 输出指定控制任务；没有样本时不将零耗时伪装成已运行。 */
static void print_control(const app_perf_snapshot_t *sample, app_perf_control_t id, const char *name)
{
    const app_perf_control_stats_t *stats = &sample->control[id];
    double cycles_per_us = (double)sample->clock_hz / (double)PERF_CYCLES_PER_US_DIVISOR;
    if (stats->count == 0u)
    {
        app_log_write("  %-8s OFF\r\n", name);
        return;
    }
    app_log_write("  %-8s avg=%5.2fus  max=%5.2fus  over=%u\r\n", name,
                  (double)stats->total_cycles / stats->count / cycles_per_us,
                  (double)stats->max_cycles / cycles_per_us, (unsigned)stats->overruns);
}

/* ---------------- task ---------------- */

/** @brief 每秒在主循环格式化日志，统计输出自身计入下一窗口有效工作。 */
void app_perf_task(void)
{
    static const char *const task_names[APP_PERF_TASK_COUNT] =
        {"CMD", "CONTROL", "HALL", "LOG", "STATUS", "PERF"};
    static const char *const irq_names[APP_PERF_IRQ_COUNT] =
        {"ADC3", "TIM1", "SYSTICK", "USART3", "DMA_TX"};
    app_perf_snapshot_t sample;
    double scale;
    if (app_perf_get_snapshot(&sample) == 0u)
    {
        // 仅在主循环重建计时与统计窗口，丢弃停表期间数据；下一秒再报告完整窗口。
        app_perf_init();
        app_log_write("[CPU] unavailable (DWT/clock/window)\r\n");
        return;
    }
    app_perf_begin_task(APP_PERF_TASK_PERF);
    scale = PERF_PERCENT_SCALE / sample.elapsed_cycles;
    app_log_write("[CPU] TASK %.2f%%  IRQ %.2f%%  POLL %.2f%%\r\n",
                  (double)(sample.work_cycles - sample.irq_cycles) * scale,
                  (double)sample.irq_cycles * scale,
                  (double)(sample.elapsed_cycles - sample.work_cycles) * scale);
    app_log_write("  TASK        CPU    IRQ         CPU\r\n");
    for (uint32_t i = 0u; i < APP_PERF_TASK_COUNT; i++)
    {
        if (i < APP_PERF_IRQ_COUNT)
        {
            app_log_write("  %-8s %6.2f%%   %-8s %6.2f%%\r\n", task_names[i],
                          (double)sample.task_cycles[i] * scale, irq_names[i],
                          (double)sample.irq_by_source[i] * scale);
        }
        else
        {
            app_log_write("  %-8s %6.2f%%\r\n", task_names[i], (double)sample.task_cycles[i] * scale);
        }
    }
    print_control(&sample, APP_PERF_CURRENT, "CURRENT");
    app_perf_end_task();
}
