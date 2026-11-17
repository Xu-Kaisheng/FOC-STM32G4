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
#define APP_PERCENT_SCALE   100.0f

/* ---------------- internal ---------------- */

/**
 * @brief  打印一行状态（含母线电压）
 * @note   ADC 链路失效时先锁存故障并关桥，再从本函数返回。
 */
static void print_status(void)
{
    adc_cur_sample_t cur = {0};
    adc_vbus_sample_t vbus = {0};

    if (app_get_adc_ready() == 0u)
    {
        app_log_write("H=%s | ADC=INVALID\r\n", bsp_hall_get_state_str(bsp_hall_read()));
        return;
    }
    if (bsp_adc_vbus_read(&vbus) == 0u)
    {
        // 母线监视链路失效也要先关桥，禁止继续打印旧电压或重新启动。
        app_set_adc_ready(0u);
        app_set_mode(APP_MODE_STOP);
        app_log_write("[FATAL] VBUS ADC read failed, reset required\r\n");
        return;
    }
    if (bsp_adc_cur_read(&cur) == 0u)
    {
        // 采样链路失效必须先关桥；锁存到复位，禁止把零值误报为正常电流。
        app_set_adc_ready(0u);
        app_set_mode(APP_MODE_STOP);
        app_log_write("[FATAL] ADC read failed, reset required\r\n");
        return;
    }

    app_log_write("H=%s f=%.1fHz D=%2.0f%% | Iu=%+5.2f Iv=%+5.2f Iw=%+5.2f A | raw=%4u,%4u,%4u\r\n",
                    bsp_hall_get_state_str(bsp_hall_read()),
                    (double)app_control_get_elec_hz(),
                    (double)(app_control_get_duty() * APP_PERCENT_SCALE),
                    (double)cur.iu, (double)cur.iv, (double)cur.iw,
                    cur.raw[0], cur.raw[1], cur.raw[2]);
    // 独立一行，避免超过串口单条日志的 127 字节上限。
    app_log_write("VBUS=%.2f V | raw=%4u\r\n", (double)vbus.voltage, vbus.raw);
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
