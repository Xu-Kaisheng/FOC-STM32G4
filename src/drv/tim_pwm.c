/**
 * @file    tim_pwm.c
 * @brief   TIM1 六路互补 PWM 驱动
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
 *   2) 提供 pwm_check_ready() 做寄存器级自检；
 *   3) 按三相自检或六步指定相组合校验输出，检查不通过时**拒绝**拉高 CTRL_SD。
 */
#include "tim_pwm.h"
#include "tim.h"
#include "main.h"
#include "../app/app_board.h"

#define PWM_ARR           ((float)app_board_pwm_arr())
#define PWM_DEADTIME_DTG  app_board_deadtime_dtg()
#define PWM_PRESCALER     0u
#define PWM_REPETITION    1u
#define PWM_CCMR_CHANNEL_SHIFT 8u

/* 占空比钳位：
 * 上限 92%：给三电阻电流采样留窗口，同时保证 IR2110 自举电容能充电；
 * 下限 5%：避免极小占空比时 1 µs 死区把脉宽吃光。 */
#define PWM_DUTY_MIN      0.05f
#define PWM_DUTY_MAX      0.92f

/* 三对互补输出在 CCER 里的使能位：CC1E/CC1NE ... CC3E/CC3NE */
#define PWM_CCER_ALL      (TIM_CCER_CC1E | TIM_CCER_CC1NE | \
                           TIM_CCER_CC2E | TIM_CCER_CC2NE | \
                           TIM_CCER_CC3E | TIM_CCER_CC3NE)
#define PWM_CCER_POLARITY (TIM_CCER_CC1P | TIM_CCER_CC1NP | \
                           TIM_CCER_CC2P | TIM_CCER_CC2NP | \
                           TIM_CCER_CC3P | TIM_CCER_CC3NP)

static const uint32_t s_phase_outputs[3] =
{
    TIM_CCER_CC1E | TIM_CCER_CC1NE,
    TIM_CCER_CC2E | TIM_CCER_CC2NE,
    TIM_CCER_CC3E | TIM_CCER_CC3NE
};

/* 三相编号 -> TIM1 通道 */
static const uint32_t s_pwm_ch[3] =
{
    TIM_CHANNEL_1,   /* PHASE_U：引脚由当前板级 TIM1_CH1/CH1N 决定 */
    TIM_CHANNEL_2,   /* PHASE_V：引脚由当前板级 TIM1_CH2/CH2N 决定 */
    TIM_CHANNEL_3    /* PHASE_W：引脚由当前板级 TIM1_CH3/CH3N 决定 */
};

/* CTRL_SD 的软件影子，供上层查询（读回 GPIO 也可以，但这样更快也更明确） */
static uint8_t s_bridge_enabled = 0u;

/**
 * @brief  把占空比限制在安全区间
 * @param  d: 原始占空比 0.0~1.0
 * @retval 钳位到 [PWM_DUTY_MIN, PWM_DUTY_MAX] 的占空比
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

/**
 * @brief  三相自检：要求六路输出成对使能，且 PWM 配置符合设计
 * @retval 1 就绪；0 未就绪
 */
uint8_t pwm_check_ready(void)
{
    return pwm_check_outputs(PWM_CCER_ALL);
}

/**
 * @brief  初始化：关桥 -> 停输出 -> 占空比清零
 * @note   上电默认必须是"桥关断、输出静默"，任何后续动作都要先通过 pwm_check_ready()
 */
void pwm_init(void)
{
    pwm_bridge_enable(0u);      /* 先断功率级，再动时序 */
    pwm_stop();                 /* 关 MOE、关六路、停计数器 */
    pwm_set_duty_raw(0.0f, 0.0f, 0.0f);
}

/**
 * @brief  启动三相 PWM：主输出 + 互补输出 + MOE + 计数器，一次到位
 * @note   先装载占空比并复位计数，再执行"MOE -> CCER -> CEN"。这里全是寄存器直写，
 *         不依赖 HAL 的状态机；启动后必须调用 pwm_check_ready() 复核。
 */
void pwm_start(void)
{
    // 关桥启动时先提交预装载，避免重启瞬间使用上次运行或自检的占空比。
    htim1.Instance->CNT = 0u;
    htim1.Instance->EGR = TIM_EGR_UG;
    htim1.Instance->BDTR |= TIM_BDTR_MOE;      /* 主输出使能 */
    htim1.Instance->CCER |= PWM_CCER_ALL;      /* 六个输出一起开 */
    __HAL_TIM_ENABLE(&htim1);                  /* 启动计数器 */
}

/**
 * @brief  停止三相 PWM：关 MOE -> 关六路 -> 停计数器
 */
void pwm_stop(void)
{
    (void)pwm_bridge_enable(0u);              /* 停 PWM 时同时保证功率级已关断 */
    htim1.Instance->BDTR &= ~(TIM_BDTR_MOE | TIM_BDTR_AOE); /* 关闭主输出并禁止自动重启 */
    htim1.Instance->CCER &= ~PWM_CCER_ALL;     /* 再关六个通道 */
    __HAL_TIM_DISABLE(&htim1);                 /* 最后停计数器 */
}

/**
 * @brief  异常紧急关断：先拉低 CTRL_SD，再关闭 TIM1 全部功率输出
 * @param  无
 * @retval 无
 * @note   故障路径不依赖 HAL 句柄、SysTick 或延时，调用后由异常处理保持停机。
 */
void pwm_emergency_stop(void)
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

/**
 * @brief  设置三相占空比（FOC 用，带 5% 下限）
 * @param  du/dv/dw: 0.0~1.0，内部会钳位
 * @note   中心对齐下 duty = CCR / ARR；CCR 预装载，下一个更新事件生效
 */
void pwm_set_duty(float du, float dv, float dw)
{
    float d[3];
    d[PHASE_U] = clamp_duty(du);
    d[PHASE_V] = clamp_duty(dv);
    d[PHASE_W] = clamp_duty(dw);

    for (uint32_t i = 0u; i < 3u; i++)
    {
        uint32_t ccr = (uint32_t)(d[i] * PWM_ARR);
        __HAL_TIM_SET_COMPARE(&htim1, s_pwm_ch[i], ccr);
    }
}

/**
 * @brief  设置三相占空比（六步用，允许 0%）
 * @param  du/dv/dw: 0.0~1.0
 * @note   六步换相时"非导通相"必须真的是 0%（下管常通），不能被钳到 5%
 */
void pwm_set_duty_raw(float du, float dv, float dw)
{
    float d[3];
    d[PHASE_U] = ((du < 0.0f) ? 0.0f : ((du > PWM_DUTY_MAX) ? PWM_DUTY_MAX : du));
    d[PHASE_V] = ((dv < 0.0f) ? 0.0f : ((dv > PWM_DUTY_MAX) ? PWM_DUTY_MAX : dv));
    d[PHASE_W] = ((dw < 0.0f) ? 0.0f : ((dw > PWM_DUTY_MAX) ? PWM_DUTY_MAX : dw));

    for (uint32_t i = 0u; i < 3u; i++)
    {
        uint32_t ccr = (uint32_t)(d[i] * PWM_ARR);
        __HAL_TIM_SET_COMPARE(&htim1, s_pwm_ch[i], ccr);
    }
}

/**
 * @brief  单相悬空 / 接通（六步换相用）
 * @param  phase: PHASE_U / PHASE_V / PHASE_W
 * @param  enable: 1 该相参与导通；0 该相上下管全关（浮空）
 * @note   只改 CCER 的两位，不动 MOE 和计数器——这是六步换相能每步切换
 *         而不打断 ADC 触发的前提。关闭后 OSSI 让上下管都停在空闲低电平。
 */
void pwm_phase_enable(pwm_phase_t phase, uint8_t enable)
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
    if ((enable != 0u) && ((app_board_drive_allowed() == 0u) ||
                            (pwm_check_outputs(expected_outputs) == 0u)))
    {
        /* 拒绝使能：PWM 未就绪时开桥等于把六个管子交给不确定状态 */
        s_bridge_enabled = 0u;
        HAL_GPIO_WritePin(CTRL_SD_GPIO_Port, CTRL_SD_Pin, GPIO_PIN_RESET);
        return 0u;
    }

    if ((enable != 0u) && (s_bridge_enabled == 0u))
    {
        // 开桥前提交 apply_step 写入的 CCR，避免首个 PWM 周期仍使用旧预装载值。
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
uint8_t pwm_bridge_enable(uint8_t enable)
{
    return pwm_bridge_apply(enable, PWM_CCER_ALL);
}

/**
 * @brief  六步模式开桥：仅允许指定两相成对使能，剩余相必须悬空
 * @param  high/low: 当前换相表中的高相与低相，不能相同
 * @retval 1 成功；0 参数或联锁失败且已关桥
 */
uint8_t pwm_bridge_enable_sixstep(pwm_phase_t high, pwm_phase_t low)
{
    if (((uint32_t)high > (uint32_t)PHASE_W) ||
        ((uint32_t)low > (uint32_t)PHASE_W) || (high == low))
    {
        (void)pwm_bridge_enable(0u);
        return 0u;
    }
    return pwm_bridge_apply(1u, s_phase_outputs[high] | s_phase_outputs[low]);
}

/**
 * @brief  查询 H 桥是否已使能
 * @retval 1 使能，0 关断
 */
uint8_t pwm_bridge_is_enabled(void)
{
    return s_bridge_enabled;
}
