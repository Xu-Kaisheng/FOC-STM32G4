/** @file app_status.h
 *  @brief 固定电角度电流环的串口波形输出。
 */
#ifndef APP_STATUS_H
#define APP_STATUS_H

#include <stdint.h>

/* ---------------- task ---------------- */

/** @brief 从主循环调度按需开启的 50 Hz 遥测。 */
void app_status_task(void);

/* ---------------- write ---------------- */

/** @brief 启动或关闭连续波形，重复启动保持节拍。 */
void app_status_set_stream(uint8_t enable);
/** @brief 在主循环关桥等待期间继续服务连续波形。 */
void app_status_wait_with_stream(uint32_t duration_ms);

#endif /* APP_STATUS_H */
