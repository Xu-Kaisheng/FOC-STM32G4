/**
 * @file    app_control.h
 * @brief   算法策略层：调节并运行六步（后续可扩展到 FOC）
 *
 * 本模块是"策略与参数"的归属地：
 *   - 持有台架可调参数（占空比、电频率、当前步号）；
 *   - 负责调用 algorithm 层的纯算法，并把 BSP 硬件接口通过回调注入算法；
 *   - 通过 app_control_task 在主循环里按步间隔推进。
 *
 * FOC 落地后，本模块负责在六步与 FOC 之间选择算法、下发参数。
 */
#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdint.h>

/* ---------------- init ---------------- */

/**
 * @brief  复位参数并把 BSP 输出接口注入算法层
 */
void app_control_init(void);

/* ---------------- task ---------------- */

/**
 * @brief  主循环控制任务：六步模式下按步间隔推进；其他模式直接返回
 */
void app_control_task(void);

/* ---------------- read ---------------- */

/** @brief 当前换相步 0~5。 */
uint8_t app_control_get_step(void);

/** @brief 当前占空比（电压幅值）。 */
float app_control_get_duty(void);

/** @brief 当前电频率，单位 Hz。 */
float app_control_get_elec_hz(void);

/**
 * @brief  查询当前步的高相与低相
 * @param  high: 高相输出地址（斩波相）
 * @param  low:  低相输出地址（下管常通相）
 * @note   开桥联锁要用同一张表，保证"写入的占空比"与"校验的输出组合"一致。
 */
void app_control_get_phases(uint8_t *high, uint8_t *low);

/* ---------------- write ---------------- */

/**
 * @brief  启动六步输出：清占空比 -> 启动 PWM -> 三相自检 -> 施加首步 -> 开桥
 * @retval 1 成功；0 自检或联锁失败，且已停 PWM
 */
uint8_t app_control_start(void);

/**
 * @brief  设置占空比并钳位到台架安全范围
 * @param  duty: 0.0~1.0
 * @retval 钳位后的实际占空比
 */
float app_control_set_duty(float duty);

/**
 * @brief  设置电频率并钳位到台架安全范围
 * @param  elec_hz: 目标电频率，单位 Hz
 * @retval 钳位后的实际电频率
 */
float app_control_set_elec_hz(float elec_hz);

#endif /* APP_CONTROL_H */
