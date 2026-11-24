/** @file app_control.c
 *  @brief 电流环启动、母线维护和故障收尾；高速计算见 app_current.c。
 */
#include "app_control.h"
#include "board.h"
#include "perf.h"
#include "pwm.h"
#include <math.h>

#define CONTROL_BUS_PERIOD_MS  10u
#define CONTROL_BUS_MAX_AGE_MS 20u
#define CONTROL_BUS_MIN_V      8.0f
#define CONTROL_BUS_MAX_V      30.0f
#define CONTROL_VREF_DRIFT_MV  50u
#define CONTROL_ZERO_DUTY      0.485f

static adc_vbus_sample_t s_bus;
static uint32_t s_last_bus_ms;
static uint16_t s_start_vref;
static uint8_t s_started;

/* ---------------- internal ---------------- */

/** @brief 采样成功且电源在台架范围内时才更新 ISR 的电压换算。 */
static uint8_t update_bus(void)
{
    adc_vbus_sample_t sample = {0};
    if ((bsp_adc_vbus_read(&sample) == 0u) ||
        ((bsp_board_get_millis() - sample.sampled_ms) > CONTROL_BUS_MAX_AGE_MS) ||
        !isfinite(sample.voltage) || (sample.voltage < CONTROL_BUS_MIN_V) ||
        (sample.voltage > CONTROL_BUS_MAX_V))
    {
        return 0u;
    }
    if ((s_started != 0u) &&
        ((sample.vref_mv + CONTROL_VREF_DRIFT_MV < s_start_vref) ||
         (sample.vref_mv > s_start_vref + CONTROL_VREF_DRIFT_MV)))
    {
        return 0u;
    }
    s_bus = sample;
    s_last_bus_ms = sample.sampled_ms;
    // 除法留在主循环；ISR 仅乘以倒数，零偏标定期间也检查样本时效。
    app_control_set_bus(1.0f / sample.voltage, sample.sampled_ms);
    return 1u;
}

/* ---------------- init ---------------- */

/** @brief 复位参数与锁存状态，必须在开中断控制前调用。 */
void app_control_init(void)
{
    s_bus = (adc_vbus_sample_t){0};
    s_last_bus_ms = 0u;
    s_started = 0u;
    app_set_fault(APP_FAULT_NONE);
    app_control_reset_current();
    (void)app_control_set_id(0.0f);
    (void)app_control_set_iq(0.0f);
    (void)app_control_set_angle(0.0f);
}

/* ---------------- task ---------------- */

/** @brief 主循环只维护慢速输入和故障清理。 */
void app_control_task(void)
{
    if ((s_started != 0u) && (app_get_fault() != APP_FAULT_NONE))
    {
        app_perf_begin_task(APP_PERF_TASK_CONTROL);
        app_control_stop();
        app_set_mode_raw(APP_MODE_STOP);
        app_perf_end_task();
    }
    if ((s_started != 0u) &&
        ((bsp_board_get_millis() - s_last_bus_ms) >= CONTROL_BUS_PERIOD_MS))
    {
        app_perf_begin_task(APP_PERF_TASK_CONTROL);
        if (update_bus() == 0u) { app_control_trip_fault(APP_FAULT_BUS); }
        app_perf_end_task();
    }
}

/* ---------------- read ---------------- */

/** @brief 母线结构仅主循环读写，无跨中断撕裂。 */
void app_control_get_bus(adc_vbus_sample_t *out)
{
    *out = s_bus;
}

/* ---------------- write ---------------- */

/** @brief 先准备三路 ADC，再启动中性 PWM；开桥留给完整零偏标定后的 ISR。 */
uint8_t app_control_start(void)
{
    if ((app_get_fault() != APP_FAULT_NONE) || (app_get_adc_ready() == 0u) ||
        (bsp_board_get_drive_allowed() == 0u)) { return 0u; }
    app_control_reset_current();
    if (update_bus() == 0u) { app_control_trip_fault(APP_FAULT_BUS); return 0u; }
    s_start_vref = s_bus.vref_mv;
    if (bsp_adc_cur_start(s_start_vref) == 0u)
    {
        app_control_trip_fault(APP_FAULT_ADC);
        return 0u;
    }
    s_started = 1u;
    bsp_pwm_set_duty(CONTROL_ZERO_DUTY, CONTROL_ZERO_DUTY, CONTROL_ZERO_DUTY);
    app_control_enable_current(1u);
    bsp_pwm_start();
    if (bsp_pwm_check_ready() == 0u)
    {
        app_control_trip_fault(APP_FAULT_PWM);
        app_control_stop();
        return 0u;
    }
    bsp_pwm_enable_update_irq(1u);
    return 1u;
}

/** @brief 先撤销开桥许可，避免停止过程中 ADC 抢占重新开桥。 */
void app_control_stop(void)
{
    app_control_enable_current(0u);
    bsp_pwm_stop();
    if (s_started != 0u)
    {
        if (bsp_adc_cur_stop() == 0u) { app_set_fault(APP_FAULT_ADC); }
    }
    s_started = 0u;
}
