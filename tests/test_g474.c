/* G474 平台驱动回归：验证三相完整发布、ADC4 独立母线及默认关桥策略。 */
#include <assert.h>
#include <stdio.h>
#include "adc.h"
#include "adc_cur.h"
#include "adc_vbus.h"
#include "board.h"

static ADC_TypeDef s_instances[4];
ADC_HandleTypeDef hadc1, hadc2, hadc3, hadc4;
uint16_t test_vref_cal = 1650u;
static uint32_t s_rank;
static uint32_t s_reference_raw = 1500u;
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
{ s_seen |= 1u << adc_index(h); if (h == &hadc4) { s_rank = 0u; } return result(h, 2u); }
/** @brief 保证只允许低频、有界主循环轮询。 */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout)
{ assert(timeout == 1u); return result(h, 3u); }
/** @brief 返回各路独立测试码。 */
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) { if ((h == &hadc4) && (s_rank++ != 0u)) { return s_reference_raw; } return s_raw[adc_index(h)]; }
/** @brief 记录失败后的转换清理。 */
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)
{ s_stops++; return result(h, 4u); }
/** @brief 板级时基接口的 HAL 替身。 */
uint32_t HAL_GetTick(void) { return 123u; }
/** @brief 板级延时接口的 HAL 替身。 */
void HAL_Delay(uint32_t delay) { (void)delay; }

/** @brief 模拟注入启动，记录每路使能和故障。 */
HAL_StatusTypeDef HAL_ADCEx_InjectedStart(ADC_HandleTypeDef *h)
{ s_seen |= 1u << adc_index(h); return result(h, 2u); }
/** @brief ADC3 作为最后完成的一路提供中断。 */
HAL_StatusTypeDef HAL_ADCEx_InjectedStart_IT(ADC_HandleTypeDef *h)
{ assert(h == &hadc3); return HAL_ADCEx_InjectedStart(h); }
/** @brief 注入停止可以失败，但必须尝试清理每个 ADC。 */
HAL_StatusTypeDef HAL_ADCEx_InjectedStop(ADC_HandleTypeDef *h)
{ s_stops++; return result(h, 4u); }

/** @brief 提供同一触发的三路新样本。 */
static void inject(uint16_t u, uint16_t v, uint16_t w)
{
    s_instances[0].JDR1 = u; s_instances[1].JDR1 = v; s_instances[2].JDR1 = w;
    for (uint32_t i = 0u; i < 3u; i++) { s_instances[i].ISR = ADC_FLAG_JEOS; }
}

/** @brief 验证零偏、三相换算、缺帧、饱和和部分启动失败清理。 */
static void test_current(void)
{
    adc_cur_sample_t cur = {0}, previous;
    assert(bsp_adc_cur_read(&cur) == 0u);
    assert(bsp_adc_cur_init() == 1u);
    assert(bsp_adc_cur_start(0u) == 0u && bsp_adc_cur_start(3601u) == 0u);
    assert(bsp_adc_cur_start(3300u) == 1u && s_seen == 7u);
    for (uint32_t i = 0u; i < 128u; i++)
    {
        inject(1550u, 1560u, 1540u);
        assert(bsp_adc_cur_read(&cur) == 1u);
        assert(bsp_adc_cur_is_calibrated() == (i == 127u));
    }
    assert(cur.iu == 0.0f && cur.iv == 0.0f && cur.iw == 0.0f);
    inject(1699u, 1485u, 1466u);
    assert(bsp_adc_cur_read(&cur) == 1u && cur.iu > 0.99f && cur.iu < 1.01f);
    assert(cur.iv < -0.49f && cur.iv > -0.51f);
    previous = cur;
    assert(bsp_adc_cur_read(&cur) == 0u && cur.iu == previous.iu);
    for (uint32_t i = 0u; i < 3u; i++)
    {
        inject(1550u, 1560u, 1540u); s_instances[i].ISR = 0u;
        assert(bsp_adc_cur_read(&cur) == 0u && cur.iu == previous.iu);
        inject(1550u, 1560u, 1540u); s_instances[i].JDR1 = 4095u;
        assert(bsp_adc_cur_read(&cur) == 0u);
        inject(1550u, 1560u, 1540u); s_instances[i].ISR |= ADC_FLAG_JQOVF;
        assert(bsp_adc_cur_read(&cur) == 0u);
    }
    assert(bsp_adc_cur_stop() == 1u);
    assert(bsp_adc_cur_start(3300u) == 1u);
    inject(3000u, 1560u, 1540u);
    assert(bsp_adc_cur_read(&cur) == 0u);
    assert(bsp_adc_cur_stop() == 1u);
    assert(bsp_adc_cur_start(3300u) == 1u);
    inject(1550u, 1560u, 1540u); assert(bsp_adc_cur_read(&cur) == 1u);
    inject(1650u, 1560u, 1540u); assert(bsp_adc_cur_read(&cur) == 0u);
    assert(bsp_adc_cur_stop() == 1u);
    for (uint32_t i = 0u; i < 3u; i++)
    {
        s_fail_index = i; s_failure = 2u; s_stops = 0u;
        assert(bsp_adc_cur_start(3300u) == 0u && s_stops == 3u);
        s_failure = 1u; assert(bsp_adc_cur_init() == 0u);
        s_failure = 0u; assert(bsp_adc_cur_init() == 1u);
    }
}

/** @brief 保留独立母线/VREF 换算、完整性与出错不发布语义回归。 */
static void test_bus(void)
{
    adc_vbus_sample_t bus = {0};
    assert(bsp_adc_vbus_read(&bus) == 0u && bsp_adc_vbus_init() == 1u);
    s_seen = 0u;
    assert(bsp_adc_vbus_read(&bus) == 1u && s_seen == 8u);
    assert(bus.raw == 1192u && bus.voltage > 24.0f && bus.voltage < 24.03f);
    s_raw[3] = 0u;
    assert(bsp_adc_vbus_read(&bus) == 1u && bus.voltage == 0.0f);
    s_raw[3] = 4095u;
    assert(bsp_adc_vbus_read(&bus) == 1u && bus.voltage > 82.47f && bus.voltage <= 82.5f);
    s_raw[3] = 4096u;
    assert(bsp_adc_vbus_read(&bus) == 0u && bus.raw == 4095u);
    s_raw[3] = 1192u;
    assert(bsp_adc_vbus_read(&bus) == 1u && bus.vref_mv == 3300u);
    s_reference_raw = 1464u;
    assert(bsp_adc_vbus_read(&bus) == 1u && bus.vref_mv == 3381u);
    s_reference_raw = 0u;
    assert(bsp_adc_vbus_read(&bus) == 0u && bus.vref_mv == 3381u);
    s_reference_raw = 4096u;
    assert(bsp_adc_vbus_read(&bus) == 0u);
    s_reference_raw = 1000u;
    assert(bsp_adc_vbus_read(&bus) == 0u);
    s_reference_raw = 1500u;
    test_vref_cal = 0u; assert(bsp_adc_vbus_init() == 0u);
    test_vref_cal = 0xffffu; assert(bsp_adc_vbus_init() == 0u);
    test_vref_cal = 1650u; assert(bsp_adc_vbus_init() == 1u);
    s_fail_index = 3u;
    for (uint32_t stage = 2u; stage <= 4u; stage++)
    {
        s_failure = stage;
        assert(bsp_adc_vbus_read(&bus) == 0u && bus.raw == 1192u);
    }
    s_failure = 1u; assert(bsp_adc_vbus_init() == 0u && bsp_adc_vbus_read(&bus) == 0u);
}

/** @brief 驱动测试入口。 */
int32_t main(void)
{
    hadc1.Instance = &s_instances[0]; hadc2.Instance = &s_instances[1];
    hadc3.Instance = &s_instances[2]; hadc4.Instance = &s_instances[3];
    test_current(); test_bus();
    assert(bsp_board_get_drive_allowed() == 1u && bsp_board_get_boot_test() == 0u);
    assert(bsp_board_get_pwm_arr() == 4250u && bsp_board_get_deadtime_dtg() == 149u);
    puts("PASS: G474 injected frames, zero calibration, startup cleanup, VBUS/VREF");
    return 0;
}
