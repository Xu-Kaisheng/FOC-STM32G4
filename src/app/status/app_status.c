/**
 * @file    app_status.c
 * @brief   周期性状态打印
 */
#include "app_status.h"
#include "app.h"
#include "app_control.h"
#include "app_log.h"
#include "app_state.h"
#include "adc_cur.h"
#include "adc_vbus.h"
#include "board.h"
#include "hall.h"
#include "perf.h"

#define APP_PRINT_PERIOD_MS 500u

/* ---------------- internal ---------------- */

/** @brief 打印控制 ISR 的完整快照，状态任务不触发或消费电流 ADC。 */
static void print_status(void)
{
    app_control_feedback_t feedback;
    adc_vbus_sample_t vbus;
    app_control_get_feedback(&feedback);
    app_control_get_bus(&vbus);
    app_log_write("FOC=%s fault=%u H=%s | Id=%+.3f/%.3f Iq=%+.3f/%.3f A\r\n",
                  (feedback.running != 0u) ? "RUN" : "OFF", (unsigned)app_get_fault(),
                  bsp_hall_get_state_str(bsp_hall_read()),
                  (double)feedback.foc.id, (double)feedback.id_ref,
                  (double)feedback.foc.iq, (double)feedback.iq_ref);
    app_log_write("Iuvw=%+.3f,%+.3f,%+.3f A | raw=%u,%u,%u | VBUS=%.2f V VREF=%u mV\r\n",
                  (double)feedback.phase.iu, (double)feedback.phase.iv, (double)feedback.phase.iw,
                  feedback.phase.raw[0], feedback.phase.raw[1], feedback.phase.raw[2],
                  (double)vbus.voltage, vbus.vref_mv);
}

/* ---------------- task ---------------- */

/**
 * @brief  周期性状态打印
 */
void app_status_task(void)
{
    uint32_t now = bsp_board_get_millis();

    if ((now - app_get_last_print_ms()) < APP_PRINT_PERIOD_MS)
    {
        return;
    }
    app_set_last_print_ms(now);
    if ((app_get_mode() != APP_MODE_STOP) || (app_get_last_hall() == HALL_INVALID))
    {
        app_set_last_hall(bsp_hall_read());
    }
    app_perf_begin_work();
    print_status();
    app_perf_end_work();
}
