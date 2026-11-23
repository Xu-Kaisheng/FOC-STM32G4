/** @file alg_foc.c
 *  @brief 三相电流坐标变换、双 PI 与连续 SVPWM。
 */
#include "alg_foc.h"
#include <math.h>

#define FOC_ONE_THIRD    0.3333333333f
#define FOC_INV_SQRT3    0.5773502692f
#define FOC_SQRT3_HALF   0.8660254038f
#define FOC_DUTY_MID     0.485f
#define FOC_DUTY_MIN     0.05f
#define FOC_DUTY_MAX     0.92f

/* ---------------- internal ---------------- */

/** @brief 对称电压限幅。 */
static float clamp_voltage(float value, float limit)
{
    if (value > limit) { return limit; }
    if (value < -limit) { return -limit; }
    return value;
}

/** @brief 饱和且误差继续推向饱和时冻结积分，反向误差可立即退饱和。 */
static float step_pi(float error, const alg_foc_config_t *config, float *integral)
{
    float candidate = clamp_voltage(*integral + config->ki_dt * error, config->axis_limit);
    float voltage = config->kp * error + candidate;
    if (((voltage > config->axis_limit) && (error > 0.0f)) ||
        ((voltage < -config->axis_limit) && (error < 0.0f)))
    {
        voltage = config->kp * error + *integral;
    }
    else
    {
        *integral = candidate;
    }
    return clamp_voltage(voltage, config->axis_limit);
}

/* ---------------- write ---------------- */

/** @brief 清除积分，防止重新启动继承上次输出。 */
void alg_foc_reset_state(alg_foc_state_t *state)
{
    state->integral_d = 0.0f;
    state->integral_q = 0.0f;
}

/** @brief 每次处理同一 PWM 周期的完整三相样本。 */
uint8_t alg_foc_step_current(const alg_foc_config_t *config, alg_foc_state_t *state,
                             const alg_foc_input_t *input, alg_foc_output_t *out)
{
    alg_foc_state_t next = *state;
    alg_foc_output_t result;
    float alpha, beta, va, vb, vc, maximum, minimum, common;

    if (!isfinite(input->iu) || !isfinite(input->iv) || !isfinite(input->iw) ||
        !isfinite(input->id_ref) || !isfinite(input->iq_ref) ||
        !isfinite(input->sin_angle) || !isfinite(input->cos_angle) ||
        !isfinite(input->inv_vbus) || (input->inv_vbus <= 0.0f))
    {
        return 0u;
    }
    // 三相 Clarke 消除测量共模；原始三相仍由调用方独立做过流检查。
    alpha = (2.0f * input->iu - input->iv - input->iw) * FOC_ONE_THIRD;
    beta = (input->iv - input->iw) * FOC_INV_SQRT3;
    result.id = alpha * input->cos_angle + beta * input->sin_angle;
    result.iq = beta * input->cos_angle - alpha * input->sin_angle;
    result.vd = step_pi(input->id_ref - result.id, config, &next.integral_d);
    result.vq = step_pi(input->iq_ref - result.iq, config, &next.integral_q);
    alpha = (result.vd * input->cos_angle - result.vq * input->sin_angle) * input->inv_vbus;
    beta = (result.vd * input->sin_angle + result.vq * input->cos_angle) * input->inv_vbus;
    va = alpha;
    vb = -0.5f * alpha + FOC_SQRT3_HALF * beta;
    vc = -0.5f * alpha - FOC_SQRT3_HALF * beta;
    maximum = fmaxf(va, fmaxf(vb, vc));
    minimum = fminf(va, fminf(vb, vc));
    common = FOC_DUTY_MID - 0.5f * (maximum + minimum);
    result.du = va + common;
    result.dv = vb + common;
    result.dw = vc + common;
    if (!isfinite(result.du) || !isfinite(result.dv) || !isfinite(result.dw) ||
        (result.du < FOC_DUTY_MIN) || (result.du > FOC_DUTY_MAX) ||
        (result.dv < FOC_DUTY_MIN) || (result.dv > FOC_DUTY_MAX) ||
        (result.dw < FOC_DUTY_MIN) || (result.dw > FOC_DUTY_MAX))
    {
        return 0u;
    }
    *state = next;
    *out = result;
    return 1u;
}
