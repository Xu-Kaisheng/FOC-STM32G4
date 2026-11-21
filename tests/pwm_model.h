/* 主机预装载模型：区分软件可见值和有效值，不模拟引脚电气行为与死区。 */
#ifndef TEST_PWM_MODEL_H
#define TEST_PWM_MODEL_H
#include <stdint.h>

typedef struct
{
    uint32_t ccmr1, ccmr2, ccer;
    uint32_t ccr[3];
    uint32_t com_count, ug_count, update_count, blocked_updates;
} test_pwm_active_t;

extern test_pwm_active_t test_pwm_active;
extern void (*test_pwm_after_write)(uint32_t *reg);

/** @brief 从当前测试寄存器复位模型与注入回调。 */
void test_pwm_reset_model(void);
/** @brief 模拟一次自然更新请求；UDIS 禁止搬运，COM 控制位不受 UEV 影响。 */
void test_pwm_update(void);
#endif
