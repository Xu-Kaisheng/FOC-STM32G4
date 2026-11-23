/* 按 RM0440 的 UEV/COM 语义维护有效寄存器，测试直接运行生产驱动代码。 */
#include "stm32g4xx_hal.h"
#include "pwm_model.h"
#include <string.h>

test_pwm_active_t test_pwm_active;
void (*test_pwm_after_write)(uint32_t *reg);

/** @brief 仅提交模式与使能，COM 不提交 CCR。 */
static void commit_control(void)
{
    test_pwm_active.ccmr1 = test_tim1.CCMR1;
    test_pwm_active.ccmr2 = test_tim1.CCMR2;
    test_pwm_active.ccer = test_tim1.CCER;
}

/** @brief 复位独立影子状态，避免跨测试继承未提交输出。 */
void test_pwm_reset_model(void)
{
    memset(&test_pwm_active, 0, sizeof(test_pwm_active));
    test_pwm_after_write = NULL;
    commit_control();
    test_pwm_active.ccr[0] = test_tim1.CCR1;
    test_pwm_active.ccr[1] = test_tim1.CCR2;
    test_pwm_active.ccr[2] = test_tim1.CCR3;
}

/** @brief 模拟自然更新请求，UDIS 期间保持旧的完整占空比组。 */
void test_pwm_update(void)
{
    if ((test_tim1.CR1 & TIM_CR1_UDIS) != 0u)
    {
        test_pwm_active.blocked_updates++;
        return;
    }
    test_pwm_active.ccr[0] = test_tim1.CCR1;
    test_pwm_active.ccr[1] = test_tim1.CCR2;
    test_pwm_active.ccr[2] = test_tim1.CCR3;
    test_pwm_active.update_count++;
}

/** @brief 拦截 CMSIS 寄存器写宏，在各写入点注入定时器事件。 */
void test_pwm_write_reg(uint32_t *reg, uint32_t value)
{
    // RM0440 规定中心对齐时 DIR 只读，不能让普通寄存器模型掩盖重启方向错误。
    if ((reg == &test_tim1.CR1) && ((*reg & TIM_CR1_CMS) != 0u))
    {
        value = (value & ~TIM_CR1_DIR) | (*reg & TIM_CR1_DIR);
    }
    *reg = value;
    if (reg == &test_tim1.EGR)
    {
        if ((value & TIM_EGR_COMG) != 0u)
        {
            if ((test_tim1.CR2 & TIM_CR2_CCPC) != 0u) { commit_control(); }
            test_pwm_active.com_count++;
        }
        if ((value & TIM_EGR_UG) != 0u)
        {
            test_tim1.CNT = 0u;
            test_pwm_update();
            test_pwm_active.ug_count++;
        }
    }
    else if ((test_tim1.CR2 & TIM_CR2_CCPC) == 0u)
    {
        commit_control();
    }
    if (test_pwm_after_write != NULL) { test_pwm_after_write(reg); }
}
