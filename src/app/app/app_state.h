/**
 * @file    app_state.h
 * @brief   应用层共享状态的唯一持有者与访问接口
 *
 * 这里只保留各模式共享的状态：运行模式、霍尔、ADC 可用性。
 * 电流环参数与积分状态归 app/control，其他模块通过接口读取。
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
    APP_MODE_FOC,           /* 固定电角度电流环调试 */
    APP_MODE_HALL_MON       /* 霍尔监视：只读不驱动 */
} app_mode_t;

typedef enum
{
    APP_FAULT_NONE = 0,
    APP_FAULT_ADC,
    APP_FAULT_OVERCURRENT,
    APP_FAULT_PWM,
    APP_FAULT_SAMPLE_LOST,
    APP_FAULT_BUS,
    APP_FAULT_CURRENT_SUM
} app_fault_t;

/* 应用层状态存储体；字段语义见 app_state.c 的默认值定义。 */
typedef struct
{
    volatile app_mode_t mode;
    volatile app_fault_t fault;
    hall_state_t last_hall;
    uint8_t      adc_ready;      /* 0 锁死启动入口，直到复位 */
} app_state_storage_t;



/* ---------------- read ---------------- */

/** @brief 当前模式。 */
app_mode_t app_get_mode(void);
/** @brief 最近一次读到的霍尔状态。 */
hall_state_t app_get_last_hall(void);
/** @brief ADC 链路是否可用；0 时禁止开桥。 */
uint8_t app_get_adc_ready(void);

/** @brief 当前锁存故障，复位前禁止重新启动。 */
app_fault_t app_get_fault(void);

/* ---------------- write ---------------- */

/** @brief 设置模式（原始写入，不跑联锁；联锁入口是 app_set_mode）。 */
void app_set_mode_raw(app_mode_t mode);
/** @brief 记录最近一次霍尔状态。 */
void app_set_last_hall(hall_state_t hall);
/** @brief 锁存/解除 ADC 可用标志。 */
void app_set_adc_ready(uint8_t ready);

/** @brief 设置锁存故障；仅初始化时清零。 */
void app_set_fault(app_fault_t fault);

#endif /* APP_STATE_H */
