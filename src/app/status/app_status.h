/**
 * @file    app_status.h
 * @brief   周期性状态打印
 */
#ifndef APP_STATUS_H
#define APP_STATUS_H

/* ---------------- task ---------------- */

/**
 * @brief  主循环状态打印任务：到周期才打印，未到周期立即返回
 * @note   ADC 采样失败会锁存故障并强制停机，禁止打印旧电压或继续运行。
 */
void app_status_task(void);

#endif /* APP_STATUS_H */
