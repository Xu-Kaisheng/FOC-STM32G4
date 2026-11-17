/** @file adc_cur.c
 *  @brief G474 三个独立 ADC 的关桥低频诊断；尚非同步电流环采样
 */
#include "adc_cur.h"
#include "adc.h"

#define ADC_PHASE_COUNT       3u
#define ADC_DIAG_TIMEOUT_MS   1u
#define ADC_DIAG_RAW_MAX      4095u

static ADC_HandleTypeDef *const s_adcs[ADC_PHASE_COUNT] = { &hadc1, &hadc2, &hadc3 };
static uint8_t s_ready = 0u;

/* ---------------- init ---------------- */

/** @brief 关桥时校准三个 ADC，任一个失败均不允许发布三相样本。 */
uint8_t bsp_adc_cur_init(void)
{
    s_ready = 0u;
    for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
    {
        if (HAL_ADCEx_Calibration_Start(s_adcs[i], ADC_SINGLE_ENDED) != HAL_OK)
        {
            return 0u;
        }
    }
    s_ready = 1u;
    return 1u;
}

/* ---------------- read ---------------- */

/** @brief 依次读取三个独立 ADC，仅用于关桥诊断，不声称三相同时采样。 */
uint8_t bsp_adc_cur_read(adc_cur_sample_t *out)
{
    adc_cur_sample_t sample = {0};
    if ((s_ready == 0u) || (out == NULL))
    {
        return 0u;
    }
    for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
    {
        uint32_t raw;
        if (HAL_ADC_Start(s_adcs[i]) != HAL_OK)
        {
            return 0u;
        }
        if (HAL_ADC_PollForConversion(s_adcs[i], ADC_DIAG_TIMEOUT_MS) != HAL_OK)
        {
            (void)HAL_ADC_Stop(s_adcs[i]);
            return 0u;
        }
        raw = HAL_ADC_GetValue(s_adcs[i]);
        if (HAL_ADC_Stop(s_adcs[i]) != HAL_OK)
        {
            return 0u;
        }
        if (raw > ADC_DIAG_RAW_MAX)
        {
            return 0u;
        }
        sample.raw[i] = (uint16_t)raw;
    }
    // 按驱动板采样链路换算，便于核对接线；零偏与 VDDA 尚待板上标定。
    sample.iu = ((float)sample.raw[0] * ADC_CUR_VREF / ADC_CUR_FULL_SCALE - ADC_CUR_OFFSET_V) / ADC_CUR_GAIN_V_PER_A;
    sample.iv = ((float)sample.raw[1] * ADC_CUR_VREF / ADC_CUR_FULL_SCALE - ADC_CUR_OFFSET_V) / ADC_CUR_GAIN_V_PER_A;
    sample.iw = ((float)sample.raw[2] * ADC_CUR_VREF / ADC_CUR_FULL_SCALE - ADC_CUR_OFFSET_V) / ADC_CUR_GAIN_V_PER_A;
    *out = sample;
    return 1u;
}
