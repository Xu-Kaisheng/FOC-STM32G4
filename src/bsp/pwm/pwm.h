/**
 * @file    pwm.h
 * @brief   TIM1 六路互补 PWM（20 kHz 中心对齐，硬件死区 1 us）
 *
 * G474 硬件映射（CubeMX 生成；完整接线见 docs/11）：
 *   PWM_UH = PA8  = TIM1_CH1     PWM_UL = PB13 = TIM1_CH1N
 *   PWM_VH = PA9  = TIM1_CH2     PWM_VL = PB14 = TIM1_CH2N
 *   PWM_WH = PA10 = TIM1_CH3     PWM_WL = PB15 = TIM1_CH3N
 *   CTRL_SD = PC9（高有效，使能 H 桥）
 *
 * 分文件规则（见 4.3 节）：
 *   pwm_io.c     纯 TIM1 时序原语，绝不触碰 CTRL_SD
 *   pwm_duty.c   占空比钳位与写入（热路径，内部零跨文件调用）
 *   pwm_bridge.c CTRL_SD 联锁、寄存器自检、紧急关断
 *   pwm.c        唯一编排点：init / start / stop
 */
#ifndef PWM_H
#define PWM_H

#include <stdint.h>
#include "main.h"

/* ---------------- 公共宏 ---------------- */

/* 三对互补输出在 CCER 里的使能位：CC1E/CC1NE ... CC3E/CC3NE。
 * 说明：这是 TIM1 寄存器掩码，供 pwm_io.c 与 pwm_bridge.c 共用；
 * 对外部模块没有语义，调用方不需要使用。 */
#define PWM_CCER_ALL      (TIM_CCER_CC1E | TIM_CCER_CC1NE | \
                           TIM_CCER_CC2E | TIM_CCER_CC2NE | \
                           TIM_CCER_CC3E | TIM_CCER_CC3NE)

/* ---------------- 公共类型 ---------------- */

/* 三相编号，和 U/V/W 一一对应 */
typedef enum
{
    PHASE_U = 0,
    PHASE_V = 1,
    PHASE_W = 2
} pwm_phase_t;

/* ---------------- init ---------------- */

/**
 * @brief  初始化：确保 H 桥关断、三路占空比归零
 */
void bsp_pwm_init(void);

/* ---------------- read ---------------- */

/**
 * @brief  三相寄存器自检：六路成对使能、MOE/CEN 有效，死区/周期/模式/极性正确
 * @retval 1 就绪；0 未就绪（此时 bsp_pwm_enable_bridge(1) 会被拒绝）
 */
uint8_t bsp_pwm_check_ready(void);

/**
 * @brief  H 桥是否已使能
 * @retval 1 使能，0 关断
 */
uint8_t bsp_pwm_is_bridge_enabled(void);

/* ---------------- write ---------------- */

/**
 * @brief  启动三相 PWM 输出（计数器运行，互补通道使能，MOE 打开）
 * @note   此时 CTRL_SD 仍为低，功率级不导通，安全
 */
void bsp_pwm_start(void);

/**
 * @brief  停止三相 PWM 输出（计数器停、输出回空闲电平），并保证关桥
 */
void bsp_pwm_stop(void);

/**
 * @brief  设置三相占空比
 * @param  du: U 相占空比 0.0~1.0（= 上管导通比例）
 * @param  dv: V 相占空比
 * @param  dw: W 相占空比
 * @note   内部会钳位到 [PWM_DUTY_MIN, PWM_DUTY_MAX]
 */
void bsp_pwm_set_duty(float du, float dv, float dw);

/**
 * @brief  设置三相占空比（不做 5% 下限钳位）
 * @param  du/dv/dw: 0.0~1.0
 * @note   六步换相专用：非导通相需要真正的 0%，让下管持续导通
 */
void bsp_pwm_set_duty_raw(float du, float dv, float dw);

/**
 * @brief  单独使能/关闭某一相的互补输出
 * @param  phase: PHASE_U / PHASE_V / PHASE_W
 * @param  enable: true 输出，false 该相上下管全关（浮空）
 * @note   六步换相时用它把不参与导通的相"悬空"
 */
void bsp_pwm_enable_phase(pwm_phase_t phase, uint8_t enable);

/**
 * @brief  控制驱动板 H 桥总开关
 * @param  enable: 1 使能（CTRL_SD 拉高），0 关断
 * @retval 1 成功；0 联锁拒绝且已关桥
 * @note   开桥要求三相全部成对使能；六步模式使用专用接口。
 */
uint8_t bsp_pwm_enable_bridge(uint8_t enable);

/**
 * @brief  六步模式开桥，检查指定两相成对使能及第三相悬空
 * @param  high/low: 当前高相与低相，必须是两个不同的有效相
 * @retval 1 成功；0 参数或联锁失败且已关桥
 */
uint8_t bsp_pwm_enable_bridge_sixstep(pwm_phase_t high, pwm_phase_t low);

/* ---------------- hook ---------------- */

/**
 * @brief  异常时立即拉低 CTRL_SD，并关闭 TIM1 输出与自动重启
 * @note   无阻塞、无延时；供致命异常路径调用，之后必须保持停机直至复位。
 *         关桥三步序列必须留在同一函数体内，不得拆散（安全红线）。
 */
void bsp_pwm_stop_emergency(void);

/* ---------------- internal ---------------- */
/* 仅供 pwm.c 编排使用；外部模块不要调用。 */

/**
 * @brief  启动 TIM1 时序（复位计数、装载预装载、MOE/CCER/CEN）
 * @note   不触碰 CTRL_SD
 */
void bsp_pwm_arm_tim(void);

/**
 * @brief  停止 TIM1 时序（清 MOE/AOE、关六路、停计数器）
 * @note   不触碰 CTRL_SD
 */
void bsp_pwm_disarm_tim(void);

#endif /* PWM_H */
