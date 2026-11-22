/** @file adc_vbus.c
 *  @brief G474 使用 ADC4 采集 PB12 母线分压及 VREFINT，不占用三相 ADC
 */
#include "adc_vbus.h"
#include "adc.h"
#include "board.h"

#define VBUS_DIVIDER       25.0f
#define VBUS_RAW_MAX       4095u
#define VBUS_TIMEOUT_MS    1u
#define VBUS_AVERAGE_COUNT 16u
#define VREF_STABLE_MS     1u
#define VREF_CAL_INVALID   0xffffu
static uint8_t s_ready = 0u;

/* ---------------- init ---------------- */

/** @brief 关桥时校准独立 ADC4。 */
uint8_t bsp_adc_vbus_init(void)
{
    s_ready = 0u;
    if (HAL_ADCEx_Calibration_Start(&hadc4, ADC_SINGLE_ENDED) != HAL_OK)
    {
        return 0u;
    }
    // CubeMX 已开启 VREFINT；等待至少 12 us，毫秒时基不依赖 DWT 是否可用。
    bsp_board_delay_ms(VREF_STABLE_MS);
    if ((*VREFINT_CAL_ADDR == 0u) || (*VREFINT_CAL_ADDR == VREF_CAL_INVALID))
    {
        return 0u;
    }
    s_ready = 1u;
    return 1u;
}

/* ---------------- read ---------------- */

/** @brief 完整采样成功后发布补偿后的母线电压，失败时不覆盖旧输出。 */
uint8_t bsp_adc_vbus_read(adc_vbus_sample_t *out)
{
    adc_vbus_sample_t sample = {0};
    uint32_t sum[2] = {0u, 0u};
    uint32_t vref_mv;
    uint32_t reference_raw;
    if ((s_ready == 0u) || (out == NULL))
    {
        return 0u;
    }
    sample.sampled_ms = bsp_board_get_millis();
    for (uint32_t i = 0u; i < VBUS_AVERAGE_COUNT; i++)
    {
        if (HAL_ADC_Start(&hadc4) != HAL_OK)
        {
            (void)HAL_ADC_Stop(&hadc4);
            return 0u;
        }
        // AutoWait 保持上一通道结果，读 DR 后才转换下一通道，防止轮询覆盖。
        for (uint32_t rank = 0u; rank < 2u; rank++)
        {
            uint32_t raw;
            if (HAL_ADC_PollForConversion(&hadc4, VBUS_TIMEOUT_MS) != HAL_OK)
            {
                (void)HAL_ADC_Stop(&hadc4);
                return 0u;
            }
            raw = HAL_ADC_GetValue(&hadc4);
            if ((raw > VBUS_RAW_MAX) || ((rank == 1u) && (raw == 0u)))
            {
                (void)HAL_ADC_Stop(&hadc4);
                return 0u;
            }
            sum[rank] += raw;
        }
        if (HAL_ADC_Stop(&hadc4) != HAL_OK)
        {
            return 0u;
        }
    }
    reference_raw = (sum[1] + VBUS_AVERAGE_COUNT / 2u) / VBUS_AVERAGE_COUNT;
    vref_mv = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(reference_raw, ADC_RESOLUTION_12B);
    if ((vref_mv < BOARD_ADC_VREF_MIN_MV) || (vref_mv > BOARD_ADC_VREF_MAX_MV))
    {
        return 0u;
    }
    sample.raw = (uint16_t)((sum[0] + VBUS_AVERAGE_COUNT / 2u) / VBUS_AVERAGE_COUNT);
    sample.vref_mv = (uint16_t)vref_mv;
    sample.voltage = (float)sample.raw * (float)vref_mv * VBUS_DIVIDER /
                     (BOARD_MV_PER_V * BOARD_ADC_FULL_SCALE);
    *out = sample;
    return 1u;
}
