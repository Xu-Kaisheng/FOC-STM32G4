/**
 * @file    app_state.c
 * @brief   应用层共享状态的存储体与存取函数
 *
 * 所有字段集中在此定义，避免拆分后出现"每个文件各存一份"。
 */
#include "app_state.h"

app_state_storage_t app_state_storage =
{
    .mode = APP_MODE_STOP,
    .last_print_ms = 0u,
    .last_hall = HALL_INVALID,
    .adc_ready = 0u
};

/* ---------------- read ---------------- */

/** @brief 当前模式。 */
app_mode_t app_get_mode(void)
{
    return app_state_storage.mode;
}

/** @brief 上一次状态打印时刻。 */
uint32_t app_get_last_print_ms(void)
{
    return app_state_storage.last_print_ms;
}

/** @brief 最近一次霍尔状态。 */
hall_state_t app_get_last_hall(void)
{
    return app_state_storage.last_hall;
}

/** @brief ADC 链路是否可用。 */
uint8_t app_get_adc_ready(void)
{
    return app_state_storage.adc_ready;
}

/* ---------------- write ---------------- */

/** @brief 设置模式。 */
void app_set_mode_raw(app_mode_t mode)
{
    app_state_storage.mode = mode;
}

/** @brief 记录状态打印时刻。 */
void app_set_last_print_ms(uint32_t millis)
{
    app_state_storage.last_print_ms = millis;
}

/** @brief 记录最近一次霍尔状态。 */
void app_set_last_hall(hall_state_t hall)
{
    app_state_storage.last_hall = hall;
}

/** @brief 锁存/解除 ADC 可用标志。 */
void app_set_adc_ready(uint8_t ready)
{
    app_state_storage.adc_ready = ready;
}
