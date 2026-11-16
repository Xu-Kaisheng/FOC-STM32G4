/**
 * @file    app_sixstep.h
 * @brief   v0.1 应用层：开环六步 + 霍尔监视
 *
 * 目的不是"把电机跑好"，而是用最小代码验证硬件通路：
 *   1) 六路 PWM 和死区是否正确
 *   2) 相序与霍尔顺序是否一致
 *   3) 三相电流采样链路是否可用
 */
#ifndef APP_SIXSTEP_H
#define APP_SIXSTEP_H

/**
 * @brief  应用初始化：PWM -> ADC -> 霍尔 -> 启动输出
 * @note   调用时 H 桥保持关断，直到用户发命令才使能
 */
void app_init(void);

/**
 * @brief  应用主循环任务（非阻塞，需在 while(1) 里反复调用）
 */
void app_task(void);

#endif /* APP_SIXSTEP_H */
