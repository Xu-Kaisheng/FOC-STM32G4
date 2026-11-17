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
 *         "钳位 + 写 CCR"必须在同一次调用内完成，拆开会在中间被中断看到
 *         一半新一半旧的三相值（见 6.2 节第 3 条）。
 */
static inline void pwm_write_ccr3(const float d[3])
{
    TIM1->CCR1 = (uint32_t)(d[PHASE_U] * PWM_ARR);
    TIM1->CCR2 = (uint32_t)(d[PHASE_V] * PWM_ARR);
    TIM1->CCR3 = (uint32_t)(d[PHASE_W] * PWM_ARR);
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
 * @brief  设置三相占空比（六步用，允许 0%）
 * @param  du/dv/dw: 0.0~1.0
 * @note   六步换相时"非导通相"必须真的是 0%（下管常通），不能被钳到 5%
 */
void bsp_pwm_set_duty_raw(float du, float dv, float dw)
{
    float d[3];

    d[PHASE_U] = ((du < 0.0f) ? 0.0f : ((du > PWM_DUTY_MAX) ? PWM_DUTY_MAX : du));
    d[PHASE_V] = ((dv < 0.0f) ? 0.0f : ((dv > PWM_DUTY_MAX) ? PWM_DUTY_MAX : dv));
    d[PHASE_W] = ((dw < 0.0f) ? 0.0f : ((dw > PWM_DUTY_MAX) ? PWM_DUTY_MAX : dw));
    pwm_write_ccr3(d);
}
