/** @file adc_cur.c
 *  @brief PWM 同步注入采样、关桥零偏标定及三相电流换算。
 */
#include "adc_cur.h"
#include "adc.h"
#include "board.h"

#define ADC_PHASE_COUNT          3u
#define ADC_RAW_MAX              4095u
#define ADC_ZERO_SAMPLES         128u
#define ADC_ZERO_RECIPROCAL      (1.0f / 128.0f)
#define ADC_ZERO_MIN_V           1.0f
#define ADC_ZERO_MAX_V           1.5f
#define ADC_ZERO_MAX_SPREAD      80u
#define ADC_CURRENT_SCALE        (1.0f / (BOARD_MV_PER_V * BOARD_ADC_FULL_SCALE * ADC_CUR_GAIN_V_PER_A))

static ADC_HandleTypeDef *const s_adcs[ADC_PHASE_COUNT] = { &hadc1, &hadc2, &hadc3 };
static uint8_t s_ready, s_sampling;
static uint32_t s_zero_count, s_sum[ADC_PHASE_COUNT];
static uint16_t s_min[ADC_PHASE_COUNT], s_max[ADC_PHASE_COUNT];
static float s_offset[ADC_PHASE_COUNT], s_amp_per_count, s_volts_per_count;

/* ---------------- init ---------------- */

/** @brief 关桥时校准三个 ADC，任一个失败均不允许发布三相样本。 */
uint8_t bsp_adc_cur_init(void)
{
    s_ready = 0u;
    s_sampling = 0u;
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

/** @brief ADC3 稍后完成，检查三路 JEOS 后一次读取；禁止轮询等待其他 ADC。 */
uint8_t bsp_adc_cur_read(adc_cur_sample_t *out)
{
    adc_cur_sample_t sample = {0};
    uint8_t valid = s_sampling;
    for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
    {
        uint32_t flags = s_adcs[i]->Instance->ISR;
        uint32_t raw = s_adcs[i]->Instance->JDR1;
        __HAL_ADC_CLEAR_FLAG(s_adcs[i], ADC_FLAG_JEOS | ADC_FLAG_JEOC | ADC_FLAG_JQOVF);
        if (((flags & ADC_FLAG_JEOS) == 0u) || ((flags & ADC_FLAG_JQOVF) != 0u) ||
            (raw == 0u) || (raw >= ADC_RAW_MAX))
        {
            valid = 0u;
        }
        sample.raw[i] = (uint16_t)raw;
    }
    if ((valid == 0u) || (out == NULL)) { return 0u; }
    if (s_zero_count < ADC_ZERO_SAMPLES)
    {
        for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
        {
            float voltage = (float)sample.raw[i] * s_volts_per_count;
            if ((voltage < ADC_ZERO_MIN_V) || (voltage > ADC_ZERO_MAX_V)) { return 0u; }
            s_sum[i] += sample.raw[i];
            if (sample.raw[i] < s_min[i]) { s_min[i] = sample.raw[i]; }
            if (sample.raw[i] > s_max[i]) { s_max[i] = sample.raw[i]; }
            if ((uint32_t)(s_max[i] - s_min[i]) > ADC_ZERO_MAX_SPREAD) { return 0u; }
        }
        s_zero_count++;
        if (s_zero_count == ADC_ZERO_SAMPLES)
        {
            for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
            {
                s_offset[i] = (float)s_sum[i] * ADC_ZERO_RECIPROCAL;
            }
        }
    }
    sample.iu = ((float)sample.raw[0] - s_offset[0]) * s_amp_per_count;
    sample.iv = ((float)sample.raw[1] - s_offset[1]) * s_amp_per_count;
    sample.iw = ((float)sample.raw[2] - s_offset[2]) * s_amp_per_count;
    *out = sample;
    return 1u;
}

/** @brief 只有完整且稳定的关桥样本窗口才允许使用零偏。 */
uint8_t bsp_adc_cur_is_calibrated(void)
{
    return (uint8_t)(s_zero_count == ADC_ZERO_SAMPLES);
}

/* ---------------- write ---------------- */

/** @brief 定时器停止时先使三路等待同一触发；部分启动失败也全部清理。 */
uint8_t bsp_adc_cur_start(uint16_t vref_mv)
{
    if ((s_ready == 0u) || (s_sampling != 0u) ||
        (vref_mv < BOARD_ADC_VREF_MIN_MV) || (vref_mv > BOARD_ADC_VREF_MAX_MV))
    {
        return 0u;
    }
    s_zero_count = 0u;
    s_volts_per_count = (float)vref_mv * (1.0f / (BOARD_MV_PER_V * BOARD_ADC_FULL_SCALE));
    s_amp_per_count = (float)vref_mv * ADC_CURRENT_SCALE;
    for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
    {
        s_sum[i] = 0u;
        s_min[i] = ADC_RAW_MAX;
        s_max[i] = 0u;
        s_offset[i] = 0.0f;
    }
    if ((HAL_ADCEx_InjectedStart(&hadc1) != HAL_OK) ||
        (HAL_ADCEx_InjectedStart(&hadc2) != HAL_OK) ||
        (HAL_ADCEx_InjectedStart_IT(&hadc3) != HAL_OK))
    {
        (void)bsp_adc_cur_stop();
        return 0u;
    }
    s_sampling = 1u;
    return 1u;
}

/** @brief 主循环关桥且停定时器后停止注入转换，HAL 等待不进入控制 ISR。 */
uint8_t bsp_adc_cur_stop(void)
{
    uint8_t success = 1u;
    s_sampling = 0u;
    __HAL_ADC_DISABLE_IT(&hadc3, ADC_IT_JEOS | ADC_IT_JEOC | ADC_IT_JQOVF);
    for (uint32_t i = 0u; i < ADC_PHASE_COUNT; i++)
    {
        if (HAL_ADCEx_InjectedStop(s_adcs[i]) != HAL_OK) { success = 0u; }
        __HAL_ADC_CLEAR_FLAG(s_adcs[i], ADC_FLAG_JEOS | ADC_FLAG_JEOC | ADC_FLAG_JQOVF);
    }
    if (success == 0u) { s_ready = 0u; }
    return success;
}
