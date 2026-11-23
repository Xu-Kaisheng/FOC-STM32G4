/**
 * @file    app_status.h
 * @brief   周期性状态打印
 */
#ifndef APP_STATUS_H
#define APP_STATUS_H

/* ---------------- task ---------------- */

/**
 * @brief  状态打印任务：由 app_task 每秒调度，与性能列表组成同一组日志
 * @note   只读取电流快照；采样故障仍由控制入口关桥锁存。
 */
void app_status_task(void);

#endif /* APP_STATUS_H */
