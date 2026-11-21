/** @file    pwm_io.c
 *  @brief   TIM1 时序原语：只操作 TIM1 寄存器，绝不触碰 CTRL_SD
 *
 * 拆出本文件的理由：`CTRL_SD` 联锁属于安全逻辑，必须与纯时序操作分开，
 * 否则任何一个"顺手写一下 GPIO"的改动都能绕过关桥检查。本文件是叶子模块，
 * 启停由 pwm.c 编排；输出组合由应用经公开接口一次提交，内部不跨模块调用。
 */
#include "pwm.h"

/* ---------------- write ---------------- */

/**
 * @brief  启动 TIM1 时序：提交三相模式 -> 复位计数并装载占空比 -> MOE -> 计数器
 * @note   这里全是寄存器直写，不依赖 HAL 状态机；启动后必须由调用方
 *         用 bsp_pwm_check_ready() 复核。本函数不触碰 CTRL_SD。
 */
void bsp_pwm_arm_tim(void)
{
    // 关桥启动时先提交预装载，避免重启瞬间使用上次运行或自检的占空比。
    CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE | TIM_BDTR_AOE);
    MODIFY_REG(TIM1->CR2, TIM_CR2_CCUS, TIM_CR2_CCPC);
    // 重启不能沿用上次低相的强制无效模式，三相自检必须恢复为 PWM1。
    MODIFY_REG(TIM1->CCMR1, TIM_CCMR1_OC1M | TIM_CCMR1_OC2M,
               TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << PWM_CCMR_CHANNEL_SHIFT));
    MODIFY_REG(TIM1->CCMR2, TIM_CCMR2_OC3M, TIM_OCMODE_PWM1);
    SET_BIT(TIM1->CCER, PWM_CCER_ALL); /* 六个输出一起预装载 */
    WRITE_REG(TIM1->EGR, TIM_EGR_COMG);
    TIM1->CNT = 0u;
    WRITE_REG(TIM1->EGR, TIM_EGR_UG);
    TIM1->BDTR |= TIM_BDTR_MOE;      /* 主输出使能 */
    TIM1->CR1 |= TIM_CR1_CEN;        /* 启动计数器 */
}

/**
 * @brief  停止 TIM1 时序：清 MOE/AOE -> 关六路 -> 停计数器
 * @note   本函数不触碰 CTRL_SD，关桥由编排层负责。
 */
void bsp_pwm_disarm_tim(void)
{
    TIM1->BDTR &= ~(TIM_BDTR_MOE | TIM_BDTR_AOE); /* 关闭主输出并禁止自动重启 */
    CLEAR_BIT(TIM1->CCER, PWM_CCER_ALL);          /* 再关六个通道 */
    WRITE_REG(TIM1->EGR, TIM_EGR_COMG);          /* 清除待提交使能，MOE 已先关闭 */
    TIM1->CR1 &= ~TIM_CR1_CEN;                    /* 最后停计数器 */
}

/**
 * @brief  通过 COM 同时提交三相模式与输出使能
 * @param  high/low: 斩波相与低侧常通相，第三相上下管关闭
 * @retval 1 已提交；0 无效参数或 COM 配置不可用
 * @note   不动 MOE 和计数器，避免换相重置载波。OSSI/OSSR 与低空闲电平由联锁检查。
 */
uint8_t bsp_pwm_apply_pair(pwm_phase_t high, pwm_phase_t low)
{
    uint32_t modes[PWM_PHASE_COUNT];
    uint32_t outputs;
    uint32_t i;

    // 禁止在缺少预装载或外部触发可能提前提交时逐相改写输出。
    if (((uint32_t)high >= PWM_PHASE_COUNT) || ((uint32_t)low >= PWM_PHASE_COUNT) ||
        (high == low) || ((TIM1->CR2 & (TIM_CR2_CCPC | TIM_CR2_CCUS)) != TIM_CR2_CCPC) ||
        ((TIM1->CR1 & TIM_CR1_CEN) == 0u) || ((TIM1->BDTR & TIM_BDTR_MOE) == 0u))
    {
        return 0u;
    }
    for (i = 0u; i < PWM_PHASE_COUNT; i++)
    {
        modes[i] = (i == (uint32_t)high) ? TIM_OCMODE_PWM1 : TIM_OCMODE_FORCED_INACTIVE;
    }
    outputs = ((TIM_CCER_CC1E | TIM_CCER_CC1NE) << ((uint32_t)high * PWM_CCER_CHANNEL_SHIFT)) |
              ((TIM_CCER_CC1E | TIM_CCER_CC1NE) << ((uint32_t)low * PWM_CCER_CHANNEL_SHIFT));
    // 强制无效作用于 OCREF，正极性互补输出使低相上管关、下管开；悬空相关闭两路。
    MODIFY_REG(TIM1->CCMR1, TIM_CCMR1_OC1M | TIM_CCMR1_OC2M,
               modes[PHASE_U] | (modes[PHASE_V] << PWM_CCMR_CHANNEL_SHIFT));
    MODIFY_REG(TIM1->CCMR2, TIM_CCMR2_OC3M, modes[PHASE_W]);
    MODIFY_REG(TIM1->CCER, PWM_CCER_ALL, outputs);
    WRITE_REG(TIM1->EGR, TIM_EGR_COMG);
    return 1u;
}
