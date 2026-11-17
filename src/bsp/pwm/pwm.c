/** @file    pwm.c
 *  @brief   PWM 唯一编排点：init / start / stop
 *
 * 只有本文件允许跨文件调用 pwm_io 与 pwm_bridge。这些函数只在初始化与
 * 停机时执行，属于冷路径，因此"拆文件"不会给 20 kHz 热路径增加任何
 * 跨文件调用开销（见 4.3 节的热路径验证）。
 */
#include "pwm.h"

/* ---------------- init ---------------- */

/**
 * @brief  初始化：关桥 -> 停输出 -> 占空比清零
 * @note   上电默认必须是"桥关断、输出静默"，任何后续动作都要先通过 bsp_pwm_check_ready()
 */
void bsp_pwm_init(void)
{
    (void)bsp_pwm_enable_bridge(0u);      /* 先断功率级，再动时序 */
    bsp_pwm_disarm_tim();                 /* 关 MOE、关六路、停计数器 */
    bsp_pwm_set_duty_raw(0.0f, 0.0f, 0.0f);
}

/* ---------------- write ---------------- */

/**
 * @brief  启动三相 PWM：主输出 + 互补输出 + MOE + 计数器，一次到位
 * @note   先装载占空比并复位计数，再执行"MOE -> CCER -> CEN"。
 *         启动后必须调用 bsp_pwm_check_ready() 复核。
 */
void bsp_pwm_start(void)
{
    bsp_pwm_arm_tim();
}

/**
 * @brief  停止三相 PWM：先关桥，再关 MOE/六路/计数器
 */
void bsp_pwm_stop(void)
{
    (void)bsp_pwm_enable_bridge(0u);      /* 停 PWM 时同时保证功率级已关断 */
    bsp_pwm_disarm_tim();
}
