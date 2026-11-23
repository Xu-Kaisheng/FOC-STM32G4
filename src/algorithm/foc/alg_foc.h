/** @file alg_foc.h
 *  @brief 标准 C 电流环；参数、积分状态和角度均由调用方持有。
 */
#ifndef ALG_FOC_H
#define ALG_FOC_H

#include <stdint.h>

typedef struct
{
    float kp;                /* V/A */
    float ki_dt;             /* Ki 乘以控制周期，V/A */
    float axis_limit;        /* 每轴电压上限，V */
} alg_foc_config_t;

typedef struct
{
    float integral_d;
    float integral_q;
} alg_foc_state_t;

typedef struct
{
    float iu, iv, iw;
    float id_ref, iq_ref;
    float sin_angle, cos_angle;
    float inv_vbus;
} alg_foc_input_t;

typedef struct
{
    float id, iq;
    float vd, vq;
    float du, dv, dw;
} alg_foc_output_t;

/* ---------------- write ---------------- */

/** @brief 清除两轴积分，启停均由调用方显式复位。 */
void alg_foc_reset_state(alg_foc_state_t *state);

/**
 * @brief 三相 Clarke/Park、抗饱和 PI、逆 Park 和共模注入 SVPWM。
 * @param config 每轴电压上限必须保证矢量落在调制范围内。
 * @retval 1 输出有效；0 输入非有限或调制越界，状态不变。
 * @note 无 HAL、全局状态、浮点除法或三角函数调用。
 */
uint8_t alg_foc_step_current(const alg_foc_config_t *config, alg_foc_state_t *state,
                             const alg_foc_input_t *input, alg_foc_output_t *out);

#endif
