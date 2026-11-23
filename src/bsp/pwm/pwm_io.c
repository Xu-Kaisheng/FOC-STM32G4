/** @file    pwm_io.c
 *  @brief   TIM1 时序原语：只操作 TIM1 寄存器，绝不触碰 CTRL_SD
 *
 * 拆出本文件的理由：`CTRL_SD` 联锁属于安全逻辑，必须与纯时序操作分开，
 * 否则任何一个"顺手写一下 GPIO"的改动都能绕过关桥检查。本文件是叶子模块，
 * 启停由 pwm.c 编排；输出组合由应用经公开接口一次提交，内部不跨模块调用。
 */
#include "pwm.h"
#include "tim.h"

/* ---------------- read ---------------- */

/** @brief 消费硬件更新标志，不等待外设。 */
uint8_t bsp_pwm_read_update(void)
{
    if ((TIM1->SR & TIM_SR_UIF) == 0u) { return 0u; }
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    return 1u;
}

/* ---------------- write ---------------- */

/**
 * @brief  启动 TIM1 时序：提交三相模式 -> 复位计数并装载占空比 -> MOE -> 计数器
 * @note   这里全是寄存器直写，不依赖 HAL 状态机；启动后必须由调用方
 *         用 bsp_pwm_check_ready() 复核。本函数不触碰 CTRL_SD。
 */
void bsp_pwm_arm_tim(void)
{
    const uint32_t counter_mode = TIM1->CR1 & TIM_CR1_CMS;
    // 关桥启动时先提交预装载，避免重启瞬间使用上次运行或自检的占空比。
    CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE | TIM_BDTR_AOE);
    CLEAR_BIT(TIM1->CR1, TIM_CR1_CEN);
    // 中心对齐时 DIR 只读；停表后暂退中心对齐，确保重启总从向上计数开始。
    CLEAR_BIT(TIM1->CR1, TIM_CR1_CMS);
    CLEAR_BIT(TIM1->CR1, TIM_CR1_DIR);
    SET_BIT(TIM1->CR1, counter_mode);
    MODIFY_REG(TIM1->CR2, TIM_CR2_CCUS, TIM_CR2_CCPC);
    // 每次启动恢复三相 PWM1，保证全部互补输出使用同一调制方式。
    MODIFY_REG(TIM1->CCMR1, TIM_CCMR1_OC1M | TIM_CCMR1_OC2M,
               TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << PWM_CCMR_CHANNEL_SHIFT));
    MODIFY_REG(TIM1->CCMR2, TIM_CCMR2_OC3M, TIM_OCMODE_PWM1);
    SET_BIT(TIM1->CCER, PWM_CCER_ALL); /* 六个输出一起预装载 */
    WRITE_REG(TIM1->EGR, TIM_EGR_COMG);
    TIM1->CNT = 0u;
    WRITE_REG(TIM1->EGR, TIM_EGR_UG);
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    TIM1->BDTR |= TIM_BDTR_MOE;      /* 主输出使能 */
    TIM1->CR1 |= TIM_CR1_CEN;        /* 启动计数器 */
}

/**
 * @brief  停止 TIM1 时序：清 MOE/AOE -> 关六路 -> 停计数器
 * @note   本函数不触碰 CTRL_SD，关桥由编排层负责。
 */
void bsp_pwm_disarm_tim(void)
{
    CLEAR_BIT(TIM1->DIER, TIM_DIER_UIE);
    TIM1->BDTR &= ~(TIM_BDTR_MOE | TIM_BDTR_AOE); /* 关闭主输出并禁止自动重启 */
    CLEAR_BIT(TIM1->CCER, PWM_CCER_ALL);          /* 再关六个通道 */
    WRITE_REG(TIM1->EGR, TIM_EGR_COMG);          /* 清除待提交使能，MOE 已先关闭 */
    TIM1->CR1 &= ~TIM_CR1_CEN;                    /* 最后停计数器 */
}


/** @brief 清除旧更新事件后开中断，避免启动 UG 被误判为漏采样。 */
void bsp_pwm_enable_update_irq(uint8_t enable)
{
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    if (enable != 0u) { SET_BIT(TIM1->DIER, TIM_DIER_UIE); }
    else { CLEAR_BIT(TIM1->DIER, TIM_DIER_UIE); }
}
