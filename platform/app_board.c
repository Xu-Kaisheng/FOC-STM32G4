/** @file app_board.c
 *  @brief G474RET6 照片识别板的诊断参数，接线未核验时不驱动功率级
 */
#include "../src/app/app_board.h"
#include "main.h"

#define BOARD_PWM_ARR         4250u
/* 170 MHz、CKD=1：(64 + 21) * 2 / 170 MHz = 1 us。 */
#define BOARD_DEADTIME_DTG    149u

/** @brief 明确显示当前是受限诊断目标。 */
const char *app_board_name(void) { return "G474RET6 DevEBox / MONITOR ONLY"; }
/** @brief 使用平台 HAL 毫秒时基。 */
uint32_t app_board_millis(void) { return HAL_GetTick(); }
/** @brief 仅用于主循环关桥后的等待。 */
void app_board_delay(uint32_t milliseconds) { HAL_Delay(milliseconds); }
/** @brief 返回 170 MHz 下 20 kHz 中心对齐周期。 */
uint32_t app_board_pwm_arr(void) { return BOARD_PWM_ARR; }
/** @brief 返回 G474 的 1 us 死区编码。 */
uint32_t app_board_deadtime_dtg(void) { return BOARD_DEADTIME_DTG; }
/** @brief 开机抓波测试已完成，恢复禁止上电 PWM 自检。 */
uint8_t app_board_boot_test(void) { return 0u; }
/** @brief 照片只能确认引出，不能证明无板载负载；默认拒绝开桥。 */
uint8_t app_board_drive_allowed(void) { return 0u; }
