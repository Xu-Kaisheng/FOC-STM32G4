/** @file app_current.c
 *  @brief 电流环状态、无锁给定和 20 kHz 采样控制入口。
 *  为何不拆：给定、斜坡、积分、反馈和看门狗共用单一 ISR 状态，拆分会扩大接口。
 */
#include "app_control.h"
#include "board.h"
#include "cycle.h"
#include "perf.h"
#include "pwm.h"
#include <math.h>

#define CURRENT_REFERENCE_MAX_A  1.0f
#define CURRENT_TRIP_A           8.0f
#define CURRENT_SUM_MAX_A        1.0f
#define CURRENT_RAMP_A_PER_TICK  0.0001f
#define CURRENT_BUS_MAX_AGE_MS   20u
#define CURRENT_DEADLINE_US      20u
#define CURRENT_DEG_TO_RAD       0.01745329252f
#define CURRENT_FULL_TURN_DEG    360.0f

/* 电阻/电感待实测，先用小电流、低电压参数做台架整定。 */
static const alg_foc_config_t s_config = {0.5f, 0.005f, 1.4f};
static alg_foc_state_t s_pi;
static volatile float s_target_d, s_target_q, s_inv_vbus;
static volatile uint32_t s_bus_ms, s_completed, s_feedback_sequence;
static volatile uint8_t s_enabled, s_running;
static volatile app_control_feedback_t s_feedback;
static uint32_t s_last_completed;
static float s_reference_d, s_reference_q, s_angle, s_sin, s_cos = 1.0f;

/* ---------------- internal ---------------- */

/** @brief 台架给定限幅，非有限值归零，避免 NaN 进入积分器。 */
static float clamp_reference(float value)
{
    if (!isfinite(value)) { return 0.0f; }
    if (value > CURRENT_REFERENCE_MAX_A) { return CURRENT_REFERENCE_MAX_A; }
    if (value < -CURRENT_REFERENCE_MAX_A) { return -CURRENT_REFERENCE_MAX_A; }
    return value;
}

/** @brief 电流命令阶跃通过固定每拍步长转为斜坡。 */
static float ramp_reference(float current, float target)
{
    if (target > current + CURRENT_RAMP_A_PER_TICK) { return current + CURRENT_RAMP_A_PER_TICK; }
    if (target < current - CURRENT_RAMP_A_PER_TICK) { return current - CURRENT_RAMP_A_PER_TICK; }
    return target;
}

/** @brief 完整采样后先检查故障，只有正常路径才提交下一拍输出。 */
static void run_current(uint32_t started)
{
    adc_cur_sample_t phase;
    alg_foc_input_t input;
    alg_foc_output_t result = {0};
    if (bsp_adc_cur_read(&phase) == 0u)
    {
        app_control_trip_fault(APP_FAULT_ADC);
        return;
    }
    if ((bsp_board_get_millis() - s_bus_ms) > CURRENT_BUS_MAX_AGE_MS)
    {
        app_control_trip_fault(APP_FAULT_BUS);
        return;
    }
    if (bsp_adc_cur_is_calibrated() == 0u) { s_completed++; return; }
    if ((fabsf(phase.iu) >= CURRENT_TRIP_A) || (fabsf(phase.iv) >= CURRENT_TRIP_A) ||
        (fabsf(phase.iw) >= CURRENT_TRIP_A))
    {
        app_control_trip_fault(APP_FAULT_OVERCURRENT);
        return;
    }
    if (fabsf(phase.iu + phase.iv + phase.iw) > CURRENT_SUM_MAX_A)
    {
        app_control_trip_fault(APP_FAULT_ADC);
        return;
    }
    if (s_running == 0u)
    {
        // 已连续确认关桥零偏，PWM 仍为中性占空比，开桥不重置计数器。
        if (bsp_pwm_enable_bridge(1u) == 0u) { app_control_trip_fault(APP_FAULT_PWM); return; }
        s_running = 1u;
    }
    s_reference_d = ramp_reference(s_reference_d, s_target_d);
    s_reference_q = ramp_reference(s_reference_q, s_target_q);
    input = (alg_foc_input_t){phase.iu, phase.iv, phase.iw,
                             s_reference_d, s_reference_q, s_sin, s_cos, s_inv_vbus};
    if ((alg_foc_step_current(&s_config, &s_pi, &input, &result) == 0u) ||
        (bsp_pwm_check_ready() == 0u) ||
        ((bsp_cycle_get_now() - started) >=
         (bsp_cycle_get_hz() / BSP_CYCLE_US_PER_S) * CURRENT_DEADLINE_US) ||
        (bsp_pwm_write_sampled(result.du, result.dv, result.dw) == 0u))
    {
        app_control_trip_fault(APP_FAULT_PWM);
        return;
    }
    s_completed++;
    // 单写者序列锁，主循环读取时允许 ISR 抢占，不关闭采样中断。
    s_feedback_sequence++;
    __DMB();
    s_feedback.phase = phase;
    s_feedback.foc = result;
    s_feedback.id_ref = s_reference_d;
    s_feedback.iq_ref = s_reference_q;
    s_feedback.samples = s_completed;
    s_feedback.running = s_running;
    __DMB();
    s_feedback_sequence++;
}

/* ---------------- read ---------------- */

/** @brief 重试被采样 ISR 打断的快照，不阻塞高优先级写者。 */
void app_control_get_feedback(app_control_feedback_t *out)
{
    uint32_t sequence;
    do
    {
        sequence = s_feedback_sequence;
        __DMB();
        *out = s_feedback;
        __DMB();
    } while (((sequence & 1u) != 0u) || (sequence != s_feedback_sequence));
    out->running = (uint8_t)((s_enabled != 0u) && (s_running != 0u));
}

/** @brief 当前 d 轴给定，主循环单写。 */
float app_control_get_id(void) { return s_target_d; }
/** @brief 当前 q 轴给定，主循环单写。 */
float app_control_get_iq(void) { return s_target_q; }
/** @brief 固定电角度仅停机时可改。 */
float app_control_get_angle(void) { return s_angle; }

/* ---------------- write ---------------- */

/** @brief 关桥且定时器停止时复位运行状态，保留用户设置的给定与角度。 */
void app_control_reset_current(void)
{
    s_enabled = 0u;
    s_running = 0u;
    s_reference_d = s_reference_q = 0.0f;
    s_completed = s_last_completed = 0u;
    s_feedback_sequence = 0u;
    s_feedback = (app_control_feedback_t){0};
    alg_foc_reset_state(&s_pi);
}

/** @brief 以单个对齐 float 发布电压倒数，时效戳在有效值之后更新。 */
void app_control_set_bus(float inv_vbus, uint32_t sampled_ms)
{
    s_inv_vbus = inv_vbus;
    __DMB();
    s_bus_ms = sampled_ms;
}

/** @brief 撤销许可先于主循环关桥，避免校准完成 ISR 在停机途中开桥。 */
void app_control_enable_current(uint8_t enable) { s_enabled = enable; }

/** @brief 更新独立 d 轴目标，32 位读写不需要临界区。 */
float app_control_set_id(float value) { s_target_d = clamp_reference(value); return s_target_d; }
/** @brief 更新独立 q 轴目标，32 位读写不需要临界区。 */
float app_control_set_iq(float value) { s_target_q = clamp_reference(value); return s_target_q; }

/** @brief 三角函数仅停机主循环计算，ISR 使用已固定的正余弦。 */
float app_control_set_angle(float degrees)
{
    if ((s_enabled != 0u) || !isfinite(degrees)) { return s_angle; }
    s_angle = fmodf(degrees, CURRENT_FULL_TURN_DEG);
    if (s_angle < 0.0f) { s_angle += CURRENT_FULL_TURN_DEG; }
    s_sin = sinf(s_angle * CURRENT_DEG_TO_RAD);
    s_cos = cosf(s_angle * CURRENT_DEG_TO_RAD);
    return s_angle;
}

/** @brief 先切断功率级，随后锁存故障，停止转换与打印交给主循环。 */
void app_control_trip_fault(app_fault_t fault)
{
    // 主循环故障也先撤销 ISR 开桥许可，防止关桥序列被校准完成中断打断。
    s_enabled = 0u;
    bsp_pwm_stop_emergency();
    s_running = 0u;
    if (app_get_fault() == APP_FAULT_NONE) { app_set_fault(fault); }
}

/* ---------------- hook ---------------- */

/** @brief ADC3 完成触发一次电流环，所有退出路径配对计时。 */
void app_control_current_ISR(void)
{
    uint32_t started;
    if (s_enabled == 0u) { return; }
    started = app_perf_begin_control();
    run_current(started);
    app_perf_end_control(APP_PERF_CURRENT, started);
}

/** @brief 下一次 PWM 更新前必须完成一帧；ADC 不来中断也能关桥。 */
void app_control_update_ISR(void)
{
    if ((bsp_pwm_read_update() == 0u) || (s_enabled == 0u)) { return; }
    if (s_completed == s_last_completed) { app_control_trip_fault(APP_FAULT_SAMPLE_LOST); }
    s_last_completed = s_completed;
}
