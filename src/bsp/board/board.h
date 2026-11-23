/** @file    board.h
 *  @brief   G474RET6 板级常量与时基（无状态、无初始化）
 *
 * 本模块是 BSP 内唯一允许被其他 BSP 模块依赖的模块：它只提供纯常量和
 * HAL 时基转发，不持有状态、不发回调，因此不会形成依赖环。
 *
 * 有意缺省 init / task / hook 三节：本模块无寄存器配置、无主循环推进、
 * 无中断入口，属于 6.5 节允许的"纯常量模块"。
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* 12 位 ADC 统一换算；参考电压范围针对本板 3.3 V 电源的诊断容差。 */
#define BOARD_ADC_FULL_SCALE 4095.0f
#define BOARD_ADC_VREF_MIN_MV 2700u
#define BOARD_ADC_VREF_MAX_MV 3600u
#define BOARD_MV_PER_V 1000.0f
/* 载波峰值前 0.5 us 触发，OC4REF 高电平宽 1 us，足够跨 ADC 时钟域。 */
#define BOARD_PWM_SAMPLE_ADVANCE_TICKS 85u

/* ---------------- read ---------------- */

/**
 * @brief  返回板级名称，用于串口显示当前固件针对的板卡
 * @retval 常量字符串
 */
const char *bsp_board_get_name(void);

/**
 * @brief  返回毫秒时基，允许 uint32_t 自然回绕
 * @retval 毫秒计数
 */
uint32_t bsp_board_get_millis(void);

/**
 * @brief  返回该板 20 kHz 中心对齐 PWM 的 ARR
 * @retval TIM1 自动重装值
 */
uint32_t bsp_board_get_pwm_arr(void);

/**
 * @brief  返回该板 1 us 硬件死区的 DTG 编码
 * @retval BDTR.DTG 期望值
 */
uint32_t bsp_board_get_deadtime_dtg(void);

/**
 * @brief  返回是否允许"关桥状态下的上电 PWM 自检"
 * @retval 1 允许；0 跳过
 */
uint8_t bsp_board_get_boot_test(void);

/**
 * @brief  返回板级接线是否已核验
 * @retval 1 允许开桥；0 时所有开桥请求都会被拒绝
 */
uint8_t bsp_board_get_drive_allowed(void);

/* ---------------- write ---------------- */

/**
 * @brief  主循环等待，仅在 H 桥已关断时调用
 * @param  milliseconds: 等待时长，单位 ms
 * @note   禁止在 ISR 调用
 */
void bsp_board_delay_ms(uint32_t milliseconds);

#endif /* BOARD_H */
