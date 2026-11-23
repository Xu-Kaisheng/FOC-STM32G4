/**
 * @file    app_status.c
 * @brief   周期性状态打印
 */
#include "app_status.h"
#include "app.h"
#include "app_control.h"
#include "app_log.h"
#include "app_state.h"
#include "hall.h"
#include "perf.h"

/* ---------------- internal ---------------- */

/** @brief 打印控制 ISR 的完整快照，状态任务不触发或消费电流 ADC。 */
static void print_status(void)
{
    app_control_feedback_t feedback;
    app_control_get_feedback(&feedback);
    app_log_write("\r\n[FOC] %-3s  fault=%u  Hall=%s\r\n",
                  (feedback.running != 0u) ? "RUN" : "OFF", (unsigned)app_get_fault(),
                  bsp_hall_get_state_str(bsp_hall_read()));
    app_log_write("  Axis(A)    Actual    Target\r\n"
                  "  Id       %+8.3f  %+8.3f\r\n"
                  "  Iq       %+8.3f  %+8.3f\r\n",
                  (double)feedback.foc.id, (double)feedback.id_ref,
                  (double)feedback.foc.iq, (double)feedback.iq_ref);
    app_log_write("  Iuvw(A)  %+8.3f  %+8.3f  %+8.3f\r\n",
                  (double)feedback.phase.iu, (double)feedback.phase.iv, (double)feedback.phase.iw);
}

/* ---------------- task ---------------- */

/**
 * @brief  由 app_task 的统一 1 s 节拍调用，与性能列表连续输出
 */
void app_status_task(void)
{
    if ((app_get_mode() != APP_MODE_STOP) || (app_get_last_hall() == HALL_INVALID))
    {
        app_set_last_hall(bsp_hall_read());
    }
    app_perf_begin_task(APP_PERF_TASK_STATUS);
    print_status();
    app_perf_end_task();
}
