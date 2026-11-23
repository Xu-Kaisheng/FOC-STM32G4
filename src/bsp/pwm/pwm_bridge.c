/** @file    pwm_bridge.c
 *  @brief   CTRL_SD 联锁、寄存器自检与紧急关断
 *
 * 【为什么这个模块要直接写寄存器】
 * HAL 的 HAL_TIM_PWM_Start() 内部只调用 TIM_CCxChannelCmd()，它只置主输出位 CCxE，
 * 从不碰互补输出位 CCxNE（HAL 把互补输出拆到了 HAL_TIMEx_PWMN_Start）。
 * 一旦漏掉 CCxNE，就会出现"上管在斩波、下管恒关"的半开桥状态：
 *   - 电流只能从母线经上管流进电机，再从另外两相的体二极管返回；
 *   - 没有任何受控续流路径，体二极管承受全部回馈电流；
 *   - 死区也失去意义（因为下管根本没被驱动）。
 * 这种状态若在 CTRL_SD=1 时出现，属于**危险工况**。
 * 所以本模块：
 *   1) 使能/关闭一律直接写 CCER 与 BDTR，成对操作六个输出；
 *   2) 提供 bsp_pwm_check_ready() 做寄存器级自检；
 *   3) 校验三相互补输出和采样触发，检查不通过时**拒绝**拉高 CTRL_SD。
 */
#include "pwm.h"
#include "board.h"
#include "main.h"

#define PWM_ARR           ((float)bsp_board_get_pwm_arr())
#define PWM_DEADTIME_DTG  bsp_board_get_deadtime_dtg()
#define PWM_PRESCALER     0u
#define PWM_REPETITION    1u

/* 三对互补输出在 CCER 里的极性位：任何一位为 1 都说明配置被改坏 */
#define PWM_CCER_POLARITY (TIM_CCER_CC1P | TIM_CCER_CC1NP | \
                           TIM_CCER_CC2P | TIM_CCER_CC2NP | \
                           TIM_CCER_CC3P | TIM_CCER_CC3NP)

/* CTRL_SD 的软件影子，供上层查询（读回 GPIO 也可以，但这样更快也更明确） */
static volatile uint8_t s_bridge_enabled = 0u;

/* ---------------- internal ---------------- */

/**
 * @brief  检查三相 PWM 的波形配置、采样触发与运行状态
 * @retval 1 全部满足；0 任一项不满足
 * @note   这是最后一道防线。CubeMX 配置被改坏（例如死区被清 0、周期被改错）时，
 *         这里会拦住，上层就不允许使能 CTRL_SD，从而避免"半开桥"带电运行。
 */
static uint8_t pwm_check_outputs(void)
{
    const uint32_t ccer = TIM1->CCER;
    const uint32_t bdtr = TIM1->BDTR;
    const uint32_t cr1  = TIM1->CR1;
    const uint32_t arr  = TIM1->ARR;

    /* 三相必须全部成对使能，避免失去受控续流路径。 */
    if (((ccer & PWM_CCER_ALL) != PWM_CCER_ALL) ||
        ((ccer & PWM_CCER_POLARITY) != 0u))
    {
        return 0u;
    }
    /* 主输出 MOE 必须为 1，否则所有输出都停在空闲电平 */
    if ((bdtr & TIM_BDTR_MOE) == 0u)
    {
        return 0u;
    }
    /* 计数器必须在跑，否则没有 PWM */
    if ((cr1 & TIM_CR1_CEN) == 0u)
    {
        return 0u;
    }
    // COM 只允许软件触发；空闲电平必须为低，UDIS 不能阻止启动时的 CCR 提交。
    if (((TIM1->CR2 & (TIM_CR2_CCPC | TIM_CR2_CCUS | TIM_CR2_OIS1 | TIM_CR2_OIS1N |
                       TIM_CR2_OIS2 | TIM_CR2_OIS2N | TIM_CR2_OIS3 | TIM_CR2_OIS3N)) != TIM_CR2_CCPC) ||
        ((cr1 & TIM_CR1_UDIS) != 0u))
    {
        return 0u;
    }
    /* 必须是设计的 1 us 死区，仅检查非零不能排除过短死区。 */
    if (((bdtr & TIM_BDTR_DTG) != PWM_DEADTIME_DTG) ||
        ((bdtr & (TIM_BDTR_OSSI | TIM_BDTR_OSSR)) != (TIM_BDTR_OSSI | TIM_BDTR_OSSR)))
    {
        return 0u;
    }
    /* 周期必须与设计值一致，否则 20 kHz 的前提不成立 */
    if ((arr != (uint32_t)PWM_ARR) || (TIM1->PSC != PWM_PRESCALER) ||
        (TIM1->RCR != PWM_REPETITION) ||
        ((cr1 & (TIM_CR1_CMS | TIM_CR1_CKD)) != TIM_COUNTERMODE_CENTERALIGNED1))
    {
        return 0u;
    }
    // 三相均为 PWM1；第四通道只在内部产生采样触发。
    if (((TIM1->CCMR1 & (TIM_CCMR1_CC1S | TIM_CCMR1_CC2S |
                        TIM_CCMR1_OC1M | TIM_CCMR1_OC2M |
                        TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE)) !=
         (TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << PWM_CCMR_CHANNEL_SHIFT) |
          TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE)) ||
        ((TIM1->CCMR2 & (TIM_CCMR2_CC3S | TIM_CCMR2_OC3M | TIM_CCMR2_OC3PE)) !=
         (TIM_OCMODE_PWM1 | TIM_CCMR2_OC3PE)))
    {
        return 0u;
    }
    if (((TIM1->CR2 & TIM_CR2_MMS2) != TIM_TRGO2_OC4REF) ||
        ((TIM1->CCMR2 & (TIM_CCMR2_CC4S | TIM_CCMR2_OC4M)) !=
         (TIM_OCMODE_PWM2 << PWM_CCMR_CHANNEL_SHIFT)) ||
        (TIM1->CCR4 != (arr - BOARD_PWM_SAMPLE_ADVANCE_TICKS)))
    {
        return 0u;
    }
    return 1u;
}

/* ---------------- read ---------------- */

/**
 * @brief  三相自检：要求六路输出成对使能，且 PWM 配置符合设计
 * @retval 1 就绪；0 未就绪
 */
uint8_t bsp_pwm_check_ready(void)
{
    return pwm_check_outputs();
}

/**
 * @brief  查询 H 桥是否已使能
 * @retval 1 使能，0 关断
 */
uint8_t bsp_pwm_is_bridge_enabled(void)
{
    return s_bridge_enabled;
}

/* ---------------- write ---------------- */

/**
 * @brief 三相联锁通过后开桥，关桥无条件执行。
 * @note 开桥在 ADC ISR 内完成，禁止 UG 重置正在运行的采样时序。
 */
uint8_t bsp_pwm_enable_bridge(uint8_t enable)
{
    if ((enable != 0u) && ((bsp_board_get_drive_allowed() == 0u) ||
        (pwm_check_outputs() == 0u) ||
        (TIM1->CCR1 < (uint32_t)(PWM_ARR * PWM_DUTY_MIN)) ||
        (TIM1->CCR2 < (uint32_t)(PWM_ARR * PWM_DUTY_MIN)) ||
        (TIM1->CCR3 < (uint32_t)(PWM_ARR * PWM_DUTY_MIN)) ||
        (TIM1->CCR1 > (uint32_t)(PWM_ARR * PWM_DUTY_MAX)) ||
        (TIM1->CCR2 > (uint32_t)(PWM_ARR * PWM_DUTY_MAX)) ||
        (TIM1->CCR3 > (uint32_t)(PWM_ARR * PWM_DUTY_MAX))))
    {
        s_bridge_enabled = 0u;
        HAL_GPIO_WritePin(CTRL_SD_GPIO_Port, CTRL_SD_Pin, GPIO_PIN_RESET);
        return 0u;
    }
    /* CTRL_SD 高有效：驱动板光耦导通 -> SD_IN 拉低 -> IR2110 使能 */
    HAL_GPIO_WritePin(CTRL_SD_GPIO_Port, CTRL_SD_Pin,
                      (enable != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_bridge_enabled = (enable != 0u) ? 1u : 0u;
    return 1u;
}

/* ---------------- hook ---------------- */

/**
 * @brief  异常紧急关断：先拉低 CTRL_SD，再关闭 TIM1 全部功率输出
 * @note   故障路径不依赖 HAL 句柄、SysTick 或延时，调用后由异常处理保持停机。
 *         "拉低 CTRL_SD -> 清 BDTR/CCER/CR1 -> 清影子态" 必须留在同一函数体内：
 *         拆开会产生可被更高优先级中断插入的窗口（安全红线，见 6.2 节）。
 */
void bsp_pwm_stop_emergency(void)
{
    // CPU 异常时先断功率级；直接使用外设地址，避免依赖可能损坏的 HAL 句柄。
    CTRL_SD_GPIO_Port->BRR = CTRL_SD_Pin;
    // 同时清 AOE，防止计数器停止前的更新事件重新置位 MOE。
    TIM1->BDTR &= ~(TIM_BDTR_MOE | TIM_BDTR_AOE);
    CLEAR_BIT(TIM1->DIER, TIM_DIER_UIE);
    CLEAR_BIT(TIM1->CCER, PWM_CCER_ALL);
    WRITE_REG(TIM1->EGR, TIM_EGR_COMG); /* MOE 已关闭，立即清除控制预装载中的使能 */
    TIM1->CR1 &= ~TIM_CR1_CEN;
    __DSB();
    s_bridge_enabled = 0u;
}
