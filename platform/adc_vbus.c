/** @file adc_vbus.c
 *  @brief G474 使用 PB12 / ADC4_IN3 采集母线分压，不占用三相 ADC
 */
#include "../src/drv/adc_vbus.h"
#include "adc.h"

#define VBUS_VREF_V       3.3f
#define VBUS_DIVIDER      25.0f
#define VBUS_COUNTS       4096.0f
#define VBUS_RAW_MAX      4095u
#define VBUS_TIMEOUT_MS   1u
static uint8_t s_ready = 0u;

/** @brief 关桥时校准独立 ADC4。 */
uint8_t adc_vbus_init(void)
{
    s_ready = 0u;
    if (HAL_ADCEx_Calibration_Start(&hadc4, ADC_SINGLE_ENDED) != HAL_OK)
    {
        return 0u;
    }
    s_ready = 1u;
    return 1u;
}

/** @brief 完整单次转换成功后发布母线电压，失败时不覆盖旧输出。 */
uint8_t adc_vbus_read(adc_vbus_sample_t *out)
{
    adc_vbus_sample_t sample = {0};
    uint32_t raw;
    if ((s_ready == 0u) || (out == NULL))
    {
        return 0u;
    }
    if (HAL_ADC_Start(&hadc4) != HAL_OK)
    {
        return 0u;
    }
    if (HAL_ADC_PollForConversion(&hadc4, VBUS_TIMEOUT_MS) != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc4);
        return 0u;
    }
    raw = HAL_ADC_GetValue(&hadc4);
    if (HAL_ADC_Stop(&hadc4) != HAL_OK)
    {
        return 0u;
    }
    if (raw > VBUS_RAW_MAX)
    {
        return 0u;
    }
    sample.raw = (uint16_t)raw;
    sample.voltage = (float)raw * (VBUS_VREF_V * VBUS_DIVIDER / VBUS_COUNTS);
    *out = sample;
    return 1u;
}
