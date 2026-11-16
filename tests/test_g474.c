/* G474 平台驱动回归：验证三相完整发布、ADC4 独立母线及默认关桥策略。 */
#include <assert.h>
#include <stdio.h>
#include "adc.h"
#include "adc_cur.h"
#include "adc_vbus.h"
#include "app_board.h"

static ADC_TypeDef s_instances[4];
ADC_HandleTypeDef hadc1, hadc2, hadc3, hadc4;
static uint32_t s_raw[4] = {1551u, 1700u, 1400u, 1192u};
static uint32_t s_fail_index, s_failure, s_stops, s_seen;

/** @brief 使用实际传入的实例识别四个独立 ADC。 */
static uint32_t adc_index(const ADC_HandleTypeDef *h)
{
    for (uint32_t i = 0u; i < 4u; i++)
    {
        if (h->Instance == &s_instances[i]) { return i; }
    }
    assert(0);
    return 0u;
}
/** @brief 在指定 ADC 的指定阶段注入失败。 */
static HAL_StatusTypeDef result(ADC_HandleTypeDef *h, uint32_t stage)
{
    return ((adc_index(h) == s_fail_index) && (s_failure == stage)) ? HAL_ERROR : HAL_OK;
}
/** @brief 模拟单端校准。 */
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t mode)
{ assert(mode == ADC_SINGLE_ENDED); return result(h, 1u); }
/** @brief 记录各路启动，确认母线不混入电流组。 */
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h)
{ s_seen |= 1u << adc_index(h); return result(h, 2u); }
/** @brief 保证只允许低频、有界主循环轮询。 */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout)
{ assert(timeout == 1u); return result(h, 3u); }
/** @brief 返回各路独立测试码。 */
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) { return s_raw[adc_index(h)]; }
/** @brief 记录失败后的转换清理。 */
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)
{ s_stops++; return result(h, 4u); }
/** @brief 板级时基接口的 HAL 替身。 */
uint32_t HAL_GetTick(void) { return 123u; }
/** @brief 板级延时接口的 HAL 替身。 */
void HAL_Delay(uint32_t delay) { (void)delay; }

/** @brief 验证 G474 正常换算与任一路失败均不发布残缺样本。 */
int32_t main(void)
{
    adc_cur_sample_t cur = {0};
    adc_cur_sample_t previous;
    adc_vbus_sample_t bus = {0};
    hadc1.Instance = &s_instances[0]; hadc2.Instance = &s_instances[1];
    hadc3.Instance = &s_instances[2]; hadc4.Instance = &s_instances[3];
    assert(adc_cur_read(&cur) == 0u && adc_vbus_read(&bus) == 0u);
    assert(adc_cur_init() == 1u && adc_vbus_init() == 1u);
    assert(adc_cur_read(NULL) == 0u && adc_vbus_read(NULL) == 0u);
    assert(adc_cur_read(&cur) == 1u && s_seen == 7u);
    assert(cur.raw[0] == 1551u && cur.iv > 0.0f && cur.iw < 0.0f);
    s_seen = 0u;
    assert(adc_vbus_read(&bus) == 1u && s_seen == 8u);
    assert(bus.raw == 1192u && bus.voltage > 24.0f && bus.voltage < 24.02f);
    s_raw[3] = 0u;
    assert(adc_vbus_read(&bus) == 1u && bus.voltage == 0.0f);
    s_raw[3] = 4095u;
    assert(adc_vbus_read(&bus) == 1u && bus.voltage > 82.47f && bus.voltage < 82.5f);
    s_raw[3] = 4096u;
    assert(adc_vbus_read(&bus) == 0u && bus.raw == 4095u);
    s_raw[3] = 1192u;
    assert(adc_vbus_read(&bus) == 1u);
    previous = cur;
    for (uint32_t i = 0u; i < 3u; i++)
    {
        s_fail_index = i;
        for (uint32_t stage = 2u; stage <= 4u; stage++)
        {
            s_failure = stage; s_stops = 0u;
            assert(adc_cur_read(&cur) == 0u);
            assert(cur.iu == previous.iu && cur.iv == previous.iv && cur.iw == previous.iw);
            assert(cur.raw[0] == previous.raw[0] && cur.raw[1] == previous.raw[1] && cur.raw[2] == previous.raw[2]);
            if (stage == 3u) { assert(s_stops == i + 1u); }
        }
        s_failure = 1u;
        assert(adc_cur_init() == 0u && adc_cur_read(&cur) == 0u);
        s_failure = 0u; assert(adc_cur_init() == 1u);
    }
    s_raw[2] = 4096u; assert(adc_cur_read(&cur) == 0u && cur.iw == previous.iw);
    s_fail_index = 3u;
    for (uint32_t stage = 2u; stage <= 4u; stage++)
    {
        s_failure = stage;
        assert(adc_vbus_read(&bus) == 0u && bus.raw == 1192u);
    }
    s_failure = 1u;
    assert(adc_vbus_init() == 0u && adc_vbus_read(&bus) == 0u);
    assert(app_board_drive_allowed() == 0u && app_board_boot_test() == 0u);
    assert(app_board_pwm_arr() == 4250u && app_board_deadtime_dtg() == 149u);
    assert(app_board_millis() == 123u);
    puts("PASS: G474 ADC1/2/3 complete diagnostics, ADC4 VBUS, monitor-only board policy");
    return 0;
}
