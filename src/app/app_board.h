/** @file app_board.h
 *  @brief 应用板级接口，不暴露 STM32 系列或 HAL 句柄
 */
#ifndef APP_BOARD_H
#define APP_BOARD_H
#include <stdint.h>

/** @brief 返回平台名称，用于区分烧录的固件。 */
const char *app_board_name(void);
/** @brief 返回毫秒时基，允许 uint32_t 自然回绕。 */
uint32_t app_board_millis(void);
/** @brief 主循环关桥后的等待，禁止在 ISR 调用。 */
void app_board_delay(uint32_t milliseconds);
/** @brief 返回该平台 20 kHz 中心对齐 PWM 的 ARR。 */
uint32_t app_board_pwm_arr(void);
/** @brief 返回该平台 1 us 硬件死区的 DTG 编码。 */
uint32_t app_board_deadtime_dtg(void);
/** @brief 返回是否允许关桥状态下的上电 PWM 自检。 */
uint8_t app_board_boot_test(void);
/** @brief 返回板级接线是否已核验；0 时所有开桥请求均拒绝。 */
uint8_t app_board_drive_allowed(void);
#endif /* APP_BOARD_H */
