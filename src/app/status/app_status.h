/** @file app_status.h
 *  @brief 固定电角度电流环的串口波形输出。
 */
#ifndef APP_STATUS_H
#define APP_STATUS_H

/* ---------------- task ---------------- */

/** @brief 从主循环调度按需开启的 50 Hz 遥测。 */
void app_status_task(void);

/* ---------------- write ---------------- */

/** @brief 按本次命令重新开始波形计时和帧同步。 */
void app_status_start_stream(void);

#endif /* APP_STATUS_H */
