/** @file    pwm_io.c
 *  @brief   TIM1 时序原语：只操作 TIM1 寄存器，绝不触碰 CTRL_SD
 *
 * 拆出本文件的理由：`CTRL_SD` 联锁属于安全逻辑，必须与纯时序操作分开，
 * 否则任何一个"顺手写一下 GPIO"的改动都能绕过关桥检查。本文件是叶子模块，
 * 只被 pwm.c 的编排函数（冷路径）调用，热路径不经过这里。
 */
#include "pwm.h"

/* ---------------- write ---------------- */

/**
 * @brief  启动 TIM1 时序：复位计数 -> 提交预装载 -> MOE -> 六路 -> 计数器
 * @note   这里全是寄存器直写，不依赖 HAL 状态机；启动后必须由调用方
 *         用 bsp_pwm_check_ready() 复核。本函数不触碰 CTRL_SD。
 */
void bsp_pwm_arm_tim(void)
{
    // 关桥启动时先提交预装载，避免重启瞬间使用上次运行或自检的占空比。
    TIM1->CNT = 0u;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->BDTR |= TIM_BDTR_MOE;      /* 主输出使能 */
    TIM1->CCER |= PWM_CCER_ALL;      /* 六个输出一起开 */
    TIM1->CR1 |= TIM_CR1_CEN;        /* 启动计数器 */
}

/**
 * @brief  停止 TIM1 时序：清 MOE/AOE -> 关六路 -> 停计数器
 * @note   本函数不触碰 CTRL_SD，关桥由编排层负责。
 */
void bsp_pwm_disarm_tim(void)
{
    TIM1->BDTR &= ~(TIM_BDTR_MOE | TIM_BDTR_AOE); /* 关闭主输出并禁止自动重启 */
    TIM1->CCER &= ~PWM_CCER_ALL;                  /* 再关六个通道 */
    TIM1->CR1 &= ~TIM_CR1_CEN;                    /* 最后停计数器 */
}

/**
 * @brief  单相悬空 / 接通（六步换相用）
 * @param  phase: PHASE_U / PHASE_V / PHASE_W
 * @param  enable: 1 该相参与导通；0 该相上下管全关（浮空）
 * @note   只改 CCER 的两位，不动 MOE 和计数器——这是六步换相能每步切换
 *         而不打断 ADC 触发的前提。关闭后 OSSI 让上下管都停在空闲低电平。
 */
void bsp_pwm_enable_phase(pwm_phase_t phase, uint8_t enable)
{
    uint32_t bit_e;
    uint32_t bit_ne;

    switch (phase)
    {
        case PHASE_U: bit_e = TIM_CCER_CC1E;  bit_ne = TIM_CCER_CC1NE;  break;
        case PHASE_V: bit_e = TIM_CCER_CC2E;  bit_ne = TIM_CCER_CC2NE;  break;
        case PHASE_W: bit_e = TIM_CCER_CC3E;  bit_ne = TIM_CCER_CC3NE;  break;
        default:      return;
    }

    if (enable != 0u)
    {
        TIM1->CCER |= (bit_e | bit_ne);
    }
    else
    {
        TIM1->CCER &= ~(bit_e | bit_ne);
    }
}
