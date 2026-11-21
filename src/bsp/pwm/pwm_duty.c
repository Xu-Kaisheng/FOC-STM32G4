/** @file    pwm_duty.c
 *  @brief   占空比钳位与写入（热路径）
 *
 * 为什么不用 __HAL_TIM_SET_COMPARE：它按通道号做三元级联再查表，
 * 在 20 kHz 电流环里是白白的额外开销。这里直接写 CCR1/2/3。
 *
 * 本文件是热路径闭环：两个公开函数内部只调用同文件的 static /
 * static inline 函数，**零跨文件调用**（见 6.2 节）。
 */
#include "pwm.h"
#include "board.h"

#define PWM_ARR           ((float)bsp_board_get_pwm_arr())

/* 占空比钳位：
 * 上限 92%：给三电阻电流采样留窗口，同时保证 IR2110 自举电容能充电；
 * 下限 5%：避免极小占空比时 1 us 死区把脉宽吃光。 */
#define PWM_DUTY_MIN      0.05f
#define PWM_DUTY_MAX      0.92f

/* ---------------- internal ---------------- */

/**
 * @brief  把占空比限制在 [PWM_DUTY_MIN, PWM_DUTY_MAX]
 * @param  d: 原始占空比
 * @retval 钳位后的占空比
 */
static float clamp_duty(float d)
{
    if (d < PWM_DUTY_MIN)
    {
        return PWM_DUTY_MIN;
    }
    if (d > PWM_DUTY_MAX)
    {
        return PWM_DUTY_MAX;
    }
    return d;
}

/**
 * @brief  把三相占空比一次性写入 CCR1/2/3
 * @param  d: 已钳位的三相占空比，索引与 PHASE_U/V/W 一致
 * @note   中心对齐下 duty = CCR / ARR；CCR 已开预装载，下一个更新事件生效。
 *         写入前先禁止 UEV 搬运，避免三次写入之间产生更新而装载半组新值。
 */
static inline void pwm_write_ccr3(const float d[3])
{
    const float arr = PWM_ARR;
    const uint32_t cu = (uint32_t)(d[PHASE_U] * arr);
    const uint32_t cv = (uint32_t)(d[PHASE_V] * arr);
    const uint32_t cw = (uint32_t)(d[PHASE_W] * arr);
    const uint32_t udis = TIM1->CR1 & TIM_CR1_UDIS;

    // 浮点换算已完成；仅在三次寄存器写入期间阻止更新，计数器不停止。
    SET_BIT(TIM1->CR1, TIM_CR1_UDIS);
    WRITE_REG(TIM1->CCR1, cu);
    WRITE_REG(TIM1->CCR2, cv);
    WRITE_REG(TIM1->CCR3, cw);
    if (udis == 0u)
    {
        CLEAR_BIT(TIM1->CR1, TIM_CR1_UDIS);
    }
}

/* ---------------- write ---------------- */

/**
 * @brief  设置三相占空比（闭环用，带 5% 下限）
 * @param  du/dv/dw: 0.0~1.0，内部会钳位
 */
void bsp_pwm_set_duty(float du, float dv, float dw)
{
    float d[3];

    d[PHASE_U] = clamp_duty(du);
    d[PHASE_V] = clamp_duty(dv);
    d[PHASE_W] = clamp_duty(dw);
    pwm_write_ccr3(d);
}

/**
 * @brief  设置三相占空比（允许初始化时清零）
 * @param  du/dv/dw: 0.0~1.0
 * @note   两相驱动时三相写相同占空比；低相常通与悬空通过模式和使能控制。
 */
void bsp_pwm_set_duty_raw(float du, float dv, float dw)
{
    float d[3];

    d[PHASE_U] = ((du < 0.0f) ? 0.0f : ((du > PWM_DUTY_MAX) ? PWM_DUTY_MAX : du));
    d[PHASE_V] = ((dv < 0.0f) ? 0.0f : ((dv > PWM_DUTY_MAX) ? PWM_DUTY_MAX : dv));
    d[PHASE_W] = ((dw < 0.0f) ? 0.0f : ((dw > PWM_DUTY_MAX) ? PWM_DUTY_MAX : dw));
    pwm_write_ccr3(d);
}
