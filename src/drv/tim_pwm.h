/**
 * @file    tim_pwm.h
 * @brief   TIM1 六路互补 PWM 驱动（20 kHz 中心对齐，硬件死区 1 us）
 *
 * G474 硬件映射（CubeMX 生成；完整接线见 docs/11）：
 *   PWM_UH = PA8  = TIM1_CH1     PWM_UL = PB13 = TIM1_CH1N
 *   PWM_VH = PA9  = TIM1_CH2     PWM_VL = PB14 = TIM1_CH2N
 *   PWM_WH = PA10 = TIM1_CH3     PWM_WL = PB15 = TIM1_CH3N
 *   CTRL_SD = PC9（高有效，使能 H 桥）
 */
#ifndef TIM_PWM_H
#define TIM_PWM_H

#include <stdint.h>

/* 三相编号，和 U/V/W 一一对应 */
typedef enum
{
    PHASE_U = 0,
    PHASE_V = 1,
    PHASE_W = 2
} pwm_phase_t;

/**
 * @brief  三相寄存器自检：六路成对使能、MOE/CEN 有效，死区/周期/模式/极性正确
 * @retval 1 就绪；0 未就绪（此时 pwm_bridge_enable(1) 会被拒绝）
 */
uint8_t pwm_check_ready(void);

/**
 * @brief  初始化：确保 H 桥关断、三路占空比归零
 */
void pwm_init(void);

/**
 * @brief  启动三相 PWM 输出（计数器运行，互补通道使能，MOE 打开）
 * @note   此时 CTRL_SD 仍为低，功率级不导通，安全
 */
void pwm_start(void);

/**
 * @brief  停止三相 PWM 输出（计数器停、输出回空闲电平）
 */
void pwm_stop(void);

/**
 * @brief  异常时立即拉低 CTRL_SD，并关闭 TIM1 输出与自动重启
 * @note   无阻塞、无延时；供致命异常路径调用，之后必须保持停机直至复位。
 */
void pwm_emergency_stop(void);

/**
 * @brief  设置三相占空比
 * @param  du: U 相占空比 0.0~1.0（= 上管导通比例）
 * @param  dv: V 相占空比
 * @param  dw: W 相占空比
 * @note   内部会钳位到 [PWM_DUTY_MIN, PWM_DUTY_MAX]
 */
void pwm_set_duty(float du, float dv, float dw);

/**
 * @brief  设置三相占空比（不做 5% 下限钳位）
 * @param  du/dv/dw: 0.0~1.0
 * @note   六步换相专用：非导通相需要真正的 0%，让下管持续导通
 */
void pwm_set_duty_raw(float du, float dv, float dw);

/**
 * @brief  单独使能/关闭某一相的互补输出
 * @param  phase: PHASE_U / PHASE_V / PHASE_W
 * @param  enable: true 输出，false 该相上下管全关（浮空）
 * @note   六步换相时用它把不参与导通的相"悬空"
 */
void pwm_phase_enable(pwm_phase_t phase, uint8_t enable);

/**
 * @brief  控制驱动板 H 桥总开关
 * @param  enable: 1 使能（CTRL_SD 拉高），0 关断
 * @retval 1 成功；0 联锁拒绝且已关桥
 * @note   开桥要求三相全部成对使能；六步模式使用专用接口。
 */
uint8_t pwm_bridge_enable(uint8_t enable);

/**
 * @brief  六步模式开桥，检查指定两相成对使能及第三相悬空
 * @param  high/low: 当前高相与低相，必须是两个不同的有效相
 * @retval 1 成功；0 参数或联锁失败且已关桥
 */
uint8_t pwm_bridge_enable_sixstep(pwm_phase_t high, pwm_phase_t low);

/**
 * @brief  H 桥是否已使能
 * @retval 1 使能，0 关断
 */
uint8_t pwm_bridge_is_enabled(void);

#endif /* TIM_PWM_H */
