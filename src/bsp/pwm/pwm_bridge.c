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
 *   3) 按三相自检或六步指定相组合校验输出，检查不通过时**拒绝**拉高 CTRL_SD。
 */
#include "pwm.h"
#include "board.h"
#include "main.h"

#define PWM_ARR           ((float)bsp_board_get_pwm_arr())
#define PWM_DEADTIME_DTG  bsp_board_get_deadtime_dtg()
#define PWM_PRESCALER     0u
#define PWM_REPETITION    1u
#define PWM_CCMR_CHANNEL_SHIFT 8u

/* 三对互补输出在 CCER 里的极性位：任何一位为 1 都说明配置被改坏 */
#define PWM_CCER_POLARITY (TIM_CCER_CC1P | TIM_CCER_CC1NP | \
                           TIM_CCER_CC2P | TIM_CCER_CC2NP | \
                           TIM_CCER_CC3P | TIM_CCER_CC3NP)

/* 单相成对输出位，用于六步模式按高/低相组合出期望掩码 */
static const uint32_t s_phase_outputs[3] =
{
    TIM_CCER_CC1E | TIM_CCER_CC1NE,
    TIM_CCER_CC2E | TIM_CCER_CC2NE,
    TIM_CCER_CC3E | TIM_CCER_CC3NE
};

/* CTRL_SD 的软件影子，供上层查询（读回 GPIO 也可以，但这样更快也更明确） */
static uint8_t s_bridge_enabled = 0u;

/* ---------------- internal ---------------- */

/**
 * @brief  按指定输出组合检查 PWM 的波形配置与运行状态
 * @param  expected_outputs: 必须精确匹配的三相输出使能位
 * @retval 1 全部满足；0 任一项不满足
 * @note   这是最后一道防线。CubeMX 配置被改坏（例如死区被清 0、周期被改错）时，
 *         这里会拦住，上层就不允许使能 CTRL_SD，从而避免"半开桥"带电运行。
 */
static uint8_t pwm_check_outputs(uint32_t expected_outputs)
{
    const uint32_t ccer = TIM1->CCER;
    const uint32_t bdtr = TIM1->BDTR;
    const uint32_t cr1  = TIM1->CR1;
    const uint32_t arr  = TIM1->ARR;

    /* 精确匹配成对输出：六步必须留一相悬空，自检则要求六路全部使能。 */
    if (((ccer & PWM_CCER_ALL) != expected_outputs) ||
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
    // 极性和 PWM1 模式必须配套，防止模式改动后占空比语义发生反转。
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
    return 1u;
}

/* ---------------- read ---------------- */

/**
 * @brief  三相自检：要求六路输出成对使能，且 PWM 配置符合设计
 * @retval 1 就绪；0 未就绪
 */
uint8_t bsp_pwm_check_ready(void)
{
    return pwm_check_outputs(PWM_CCER_ALL);
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
 * @brief  按预期输出组合控制驱动板 H 桥总开关（CTRL_SD）
 * @param  enable: 1 使能，0 关断
 * @param  expected_outputs: 预期成对使能的输出组合
 * @retval 1 成功；0 联锁拒绝且已关桥
 * @note   **安全联锁**：只有指定输出组合检查通过才允许拉高 CTRL_SD。
 *         因为一旦桥被使能而 PWM 处于半开状态，功率级就会进入非受控导通，
 *         轻则电流不对称、体二极管过热，重则烧管。
 */
static uint8_t pwm_bridge_apply(uint8_t enable, uint32_t expected_outputs)
{
    if ((enable != 0u) && ((bsp_board_get_drive_allowed() == 0u) ||
                            (pwm_check_outputs(expected_outputs) == 0u)))
    {
        /* 拒绝使能：PWM 未就绪时开桥等于把六个管子交给不确定状态 */
        s_bridge_enabled = 0u;
        HAL_GPIO_WritePin(CTRL_SD_GPIO_Port, CTRL_SD_Pin, GPIO_PIN_RESET);
        return 0u;
    }

    if ((enable != 0u) && (s_bridge_enabled == 0u))
    {
        // 开桥前提交控制层写入的 CCR，避免首个 PWM 周期仍使用旧预装载值。
        TIM1->EGR = TIM_EGR_UG;
    }
    /* CTRL_SD 高有效：驱动板光耦导通 -> SD_IN 拉低 -> IR2110 使能 */
    HAL_GPIO_WritePin(CTRL_SD_GPIO_Port, CTRL_SD_Pin,
                      (enable != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_bridge_enabled = (enable != 0u) ? 1u : 0u;
    return 1u;
}

/**
 * @brief  三相 PWM 的桥使能接口；关桥不要求任何输出已就绪
 * @param  enable: 1 使能，0 关断
 * @retval 1 成功；0 联锁拒绝且已关桥
 */
uint8_t bsp_pwm_enable_bridge(uint8_t enable)
{
    return pwm_bridge_apply(enable, PWM_CCER_ALL);
}

/**
 * @brief  六步模式开桥：仅允许指定两相成对使能，剩余相必须悬空
 * @param  high/low: 当前换相表中的高相与低相，不能相同
 * @retval 1 成功；0 参数或联锁失败且已关桥
 */
uint8_t bsp_pwm_enable_bridge_sixstep(pwm_phase_t high, pwm_phase_t low)
{
    if (((uint32_t)high > (uint32_t)PHASE_W) ||
        ((uint32_t)low > (uint32_t)PHASE_W) || (high == low))
    {
        (void)bsp_pwm_enable_bridge(0u);
        return 0u;
    }
    return pwm_bridge_apply(1u, s_phase_outputs[high] | s_phase_outputs[low]);
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
    TIM1->CCER &= ~PWM_CCER_ALL;
    TIM1->CR1 &= ~TIM_CR1_CEN;
    __DSB();
    s_bridge_enabled = 0u;
}
