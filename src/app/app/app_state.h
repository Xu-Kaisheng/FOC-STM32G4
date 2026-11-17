/**
 * @file    app_state.h
 * @brief   应用层共享状态的唯一持有者与访问接口
 *
 * 这里只保留各模式共享的状态：运行模式、时间戳、霍尔、ADC 可用性。
 * 六步专用参数（步号、占空比、电频率）归 `sixstep` 模块，FOC 未来会有
 * 自己的状态，避免把算法私有数据堆进共享结构。
 *
 * 约定：`app_state_storage` 是内部存储体，除本头文件的 inline 访问器与
 * app_state.c 外，任何模块都不得直接读写它的字段。
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include "hall.h"

/* ---------------- 公共类型 ---------------- */

/* 运行模式 */
typedef enum
{
    APP_MODE_STOP = 0,      /* 停机：H 桥关断 */
    APP_MODE_SIXSTEP_FWD,   /* 开环六步，正转 */
    APP_MODE_SIXSTEP_REV,   /* 开环六步，反转 */
    APP_MODE_HALL_MON       /* 霍尔监视：只读不驱动 */
} app_mode_t;

/* 应用层状态存储体；字段语义见 app_state.c 的默认值定义。 */
typedef struct
{
    app_mode_t   mode;
    uint32_t     last_print_ms;
    hall_state_t last_hall;
    uint8_t      adc_ready;      /* 0 锁死启动入口，直到复位 */
} app_state_storage_t;

extern app_state_storage_t app_state_storage;

/* ---------------- read ---------------- */

/** @brief 当前模式。 */
app_mode_t app_get_mode(void);
/** @brief 上一次状态打印时刻，单位 ms。 */
uint32_t app_get_last_print_ms(void);
/** @brief 最近一次读到的霍尔状态。 */
hall_state_t app_get_last_hall(void);
/** @brief ADC 链路是否可用；0 时禁止开桥。 */
uint8_t app_get_adc_ready(void);

/* ---------------- write ---------------- */

/** @brief 设置模式（原始写入，不跑联锁；联锁入口是 app_set_mode）。 */
void app_set_mode_raw(app_mode_t mode);
/** @brief 记录状态打印时刻。 */
void app_set_last_print_ms(uint32_t millis);
/** @brief 记录最近一次霍尔状态。 */
void app_set_last_hall(hall_state_t hall);
/** @brief 锁存/解除 ADC 可用标志。 */
void app_set_adc_ready(uint8_t ready);

#endif /* APP_STATE_H */
