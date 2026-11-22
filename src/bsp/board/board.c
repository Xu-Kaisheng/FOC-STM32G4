/** @file    board.c
 *  @brief   G474RET6 DevEBox 板级参数，允许命令启动，上电保持关桥
 */
#include "board.h"
#include "main.h"

/* 170 MHz、中心对齐、PSC=0、RCR=1：ARR=4250 对应 20 kHz。 */
#define BOARD_PWM_ARR         4250u
/* 170 MHz、CKD=1：(64 + 21) * 2 / 170 MHz = 1 us。 */
#define BOARD_DEADTIME_DTG    149u

/* ---------------- read ---------------- */

/** @brief 显示板卡名称；是否运行由应用模式决定。 */
const char *bsp_board_get_name(void)
{
    return "G474RET6 DevEBox";
}

/** @brief 使用 HAL 毫秒时基。 */
uint32_t bsp_board_get_millis(void)
{
    return HAL_GetTick();
}

/** @brief 返回 170 MHz 下 20 kHz 中心对齐周期。 */
uint32_t bsp_board_get_pwm_arr(void)
{
    return BOARD_PWM_ARR;
}

/** @brief 返回 G474 的 1 us 死区编码。 */
uint32_t bsp_board_get_deadtime_dtg(void)
{
    return BOARD_DEADTIME_DTG;
}

/** @brief 开机抓波测试已完成，恢复禁止上电 PWM 自检。 */
uint8_t bsp_board_get_boot_test(void)
{
    return 0u;
}

/** @brief 按维护者许可开放手动启动，开桥仍须通过 ADC 与 PWM 联锁。 */
uint8_t bsp_board_get_drive_allowed(void)
{
    return 1u;
}

/* ---------------- write ---------------- */

/** @brief 仅用于主循环关桥后的等待。 */
void bsp_board_delay_ms(uint32_t milliseconds)
{
    HAL_Delay(milliseconds);
}
