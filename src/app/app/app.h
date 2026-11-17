/**
 * @file    app.h
 * @brief   v0.1 应用主循环：初始化、模式管理、调度入口
 *
 * 初始化、模式联锁与主循环调度共用同一份状态与日志上下文，合并在一个
 * 模块里维护，避免为"文件小"而拆出三个互相 include 的文件。
 */
#ifndef APP_H
#define APP_H

#include "app_state.h"

/* ---------------- init ---------------- */

/**
 * @brief  应用初始化：串口 -> PWM -> ADC -> 霍尔 -> 启动输出
 * @note   调用时 H 桥保持关断，直到用户发命令才使能
 */
void app_init(void);

/* ---------------- task ---------------- */

/**
 * @brief  应用主循环任务（非阻塞，需在 while(1) 里反复调用）
 */
void app_task(void);

/* ---------------- write ---------------- */

/**
 * @brief  进入指定模式
 * @param  mode: 目标模式
 * @note   任何切换都先关桥、停 PWM，再等待驱动板过流锁存复位时间；
 *         开桥前必须通过 bsp_pwm_check_ready() 与六步联锁。
 */
void app_set_mode(app_mode_t mode);

#endif /* APP_H */
