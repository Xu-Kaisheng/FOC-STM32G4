/* 编译实际 src 源文件，通过 HAL 替身验证启动、错误路径和串口调度。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "adc_cur.h"
#include "adc_vbus.h"
#include "pwm.h"
#include "hall.h"
#include "uart.h"
#include "app.h"
#include "app_log.h"
#include "board.h"
#include "app_cmd.h"
#include "app_state.h"
#include "perf.h"
#include "perf_clock.h"
#include "stm32g4xx_it.h"
#include "pwm_model.h"
#include "app_control.h"
#include <math.h>

/* 可控执行耗时只用于核对统计区间，不代表实板测量。 */
#define TEST_RX_CYCLES    13u
#define TEST_TX_CYCLES    29u
#define TEST_DELAY_CYCLES 37u
#define TEST_PWM_DUTY       0.20f
#define TEST_PWM_COMPARE    850u
#define TEST_PWM_NEXT_DUTY  0.40f
#define TEST_PWM_NEXT_CCR   1700u
#define TEST_PWM_COUNT     1234u

GPIO_TypeDef test_gpioc;
TIM_TypeDef test_tim1;
static ADC_TypeDef s_adcs[4];
static USART_TypeDef s_uart;
static char s_tx_capture[4096];
static uint32_t s_tx_length;
static uint8_t s_tx_dma_busy;
ADC_HandleTypeDef hadc1, hadc2, hadc3, hadc4;
uint16_t test_vref_cal = 1650u;
static uint32_t s_rank;
static uint32_t s_reference_raw = 1500u;
static HAL_StatusTypeDef s_reference_poll;
static uint32_t s_reference_delay_ms;
TIM_HandleTypeDef htim1;
UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_tx;
static uint8_t s_measure_cycles;
static uint32_t s_tick;
static uint8_t s_rx;
static uint8_t s_board_allow = 1u;
static uint8_t s_boot_test = 1u;
static uint8_t s_verify_boot;
static uint32_t s_boot_pwm_ticks;
static HAL_StatusTypeDef s_calibration, s_start, s_poll, s_stop;
static HAL_StatusTypeDef s_vbus_calibration, s_vbus_start, s_vbus_poll, s_vbus_stop;
static uint32_t s_vbus_raw, s_vbus_stop_calls;


/** @brief 初始化测试寄存器及可注入错误。 */
static void reset_hardware(void)
{
    memset(&test_tim1, 0, sizeof(test_tim1));
    memset(&test_gpioc, 0, sizeof(test_gpioc));
    memset(s_adcs, 0, sizeof(s_adcs));
    memset(&hadc1, 0, sizeof(hadc1));
    memset(&s_uart, 0, sizeof(s_uart));
    htim1.Instance = TIM1;
    hadc1.Instance = &s_adcs[0];
    hadc2.Instance = &s_adcs[1];
    hadc3.Instance = &s_adcs[2];
    hadc4.Instance = &s_adcs[3];
    huart3.Instance = &s_uart;
    huart3.ErrorCode = 0u;
    test_tim1.ARR = 4250u;
    test_tim1.RCR = 1u;
    test_tim1.CR1 = TIM_COUNTERMODE_CENTERALIGNED1;
    test_tim1.CR2 = TIM_TRGO2_OC4REF;
    test_tim1.CCR4 = 4165u;
    test_tim1.BDTR = 149u | TIM_BDTR_OSSI | TIM_BDTR_OSSR | TIM_BDTR_AOE;
    test_tim1.CCMR1 = TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << 8u) | TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE;
    test_tim1.CCMR2 = TIM_OCMODE_PWM1 | TIM_CCMR2_OC3PE | (TIM_OCMODE_PWM2 << 8u);
    s_uart.ISR = 0u;
    s_tx_length = 0u;
    s_tx_dma_busy = 0u;
    s_calibration = s_start = s_poll = s_stop = HAL_OK;
    s_vbus_calibration = s_vbus_start = s_vbus_poll = s_vbus_stop = HAL_OK;
    s_vbus_raw = 1192u;
    s_reference_poll = HAL_OK;
    s_reference_delay_ms = 0u;
    s_vbus_stop_calls = 0u;
    s_tick = 0u;
    s_rx = 0u;
    s_board_allow = 1u;
    s_boot_test = 0u;
    s_verify_boot = 0u;
    s_boot_pwm_ticks = 0u;
    s_measure_cycles = 0u;
    test_pwm_reset_model();
    app_control_init();
    app_set_mode_raw(APP_MODE_STOP);
}

/** @brief 模拟 CCR 写入；不模拟硬件预装载和死区。 */
void test_set_compare(TIM_HandleTypeDef *h, uint32_t channel, uint32_t value)
{
    switch (channel)
    {
        case TIM_CHANNEL_1: h->Instance->CCR1 = value; break;
        case TIM_CHANNEL_2: h->Instance->CCR2 = value; break;
        case TIM_CHANNEL_3: h->Instance->CCR3 = value; break;
        default: assert(0); break;
    }
}
/** @brief 模拟 GPIO 输出电平。 */
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) { port->ODR |= pin; }
    else { port->ODR &= ~(uint32_t)pin; }
}
/** @brief 模拟 GPIO 输入。 */
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return ((port->IDR & pin) != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
/** @brief 自动推进测试时钟，使自检等待可在主机执行。 */
uint32_t HAL_GetTick(void)
{
    if (s_verify_boot != 0u)
    {
        assert((test_gpioc.ODR & CTRL_SD_Pin) == 0u);
        if ((test_tim1.CR1 & TIM_CR1_CEN) != 0u)
        {
            assert(bsp_pwm_check_ready() == 1u);
            assert(test_tim1.CCR1 == 2125u && test_tim1.CCR2 == 2125u && test_tim1.CCR3 == 2125u);
            s_boot_pwm_ticks++;
        }
    }
    if (s_verify_boot != 0u) { s_tick++; }
    return s_tick;
}
/** @brief 模拟等待，并确认等待前已关桥。 */
void HAL_Delay(uint32_t delay)
{
    assert((test_gpioc.ODR & CTRL_SD_Pin) == 0u);
    s_tick += delay;
    if (s_measure_cycles != 0u) { test_dwt.CYCCNT += TEST_DELAY_CYCLES; }
}

/** @brief 模拟系统节拍的中断处理耗时。 */
void HAL_IncTick(void)
{
    s_tick++;
    test_dwt.CYCCNT += 7u;
}

/** @brief 模拟 DMA 中断被高优先级 SysTick 抢占，检查嵌套不重复计时。 */
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h)
{
    assert(h == &hdma_usart3_tx && test_primask == 0u);
    test_dwt.CYCCNT += 11u;
    SysTick_Handler();
    test_dwt.CYCCNT += 13u;
}

/** @brief 模拟 USART 完成中断，确认统计钩子没有屏蔽整个 HAL 处理。 */
void HAL_UART_IRQHandler(UART_HandleTypeDef *h)
{
    assert(h == &huart3 && test_primask == 0u);
    test_dwt.CYCCNT += 19u;
}

/** @brief 测试板名，与生产板级实现分离。 */
const char *bsp_board_get_name(void) { return "TEST"; }
/** @brief 接入可控测试时基。 */
uint32_t bsp_board_get_millis(void) { return HAL_GetTick(); }
/** @brief 验证模式切换先关桥再等待。 */
void bsp_board_delay_ms(uint32_t ms) { HAL_Delay(ms); }
/** @brief 使用 G474 基线参数。 */
uint32_t bsp_board_get_pwm_arr(void) { return 4250u; }
/** @brief 使用 G474 硬件死区参数。 */
uint32_t bsp_board_get_deadtime_dtg(void) { return 149u; }
/** @brief 独立控制上电自检，覆盖手动驱动许可与自动输出分离的策略。 */
uint8_t bsp_board_get_boot_test(void) { return s_boot_test; }
/** @brief 模拟 G474 未核验接线的开桥禁用状态。 */
uint8_t bsp_board_get_drive_allowed(void) { return s_board_allow; }

/** @brief 将错误注入 G474 的 ADC1/2/3 或独立 ADC4。 */
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t mode)
{
    assert(mode == ADC_SINGLE_ENDED);
    return (h == &hadc4) ? s_vbus_calibration : s_calibration;
}
/** @brief 模拟规则组启动，保留应用层故障回归。 */
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h)
{ if (h == &hadc4) { s_rank = 0u; } return (h == &hadc4) ? s_vbus_start : s_start; }
/** @brief 所有诊断 ADC 的主循环轮询均有 1 ms 超时参数。 */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout)
{
    assert(timeout == 1u);
    if ((h == &hadc4) && (s_rank == 1u))
    {
        s_tick += s_reference_delay_ms;
        if (s_reference_poll != HAL_OK) { return s_reference_poll; }
    }
    return (h == &hadc4) ? s_vbus_poll : s_poll;
}
/** @brief 提供三相测试码及独立母线测试码。 */
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h)
{
    if (h == &hadc4) { return (s_rank++ == 0u) ? s_vbus_raw : s_reference_raw; }
    if (h == &hadc1) { return 1551u; }
    if (h == &hadc2) { return 1700u; }
    assert(h == &hadc3);
    return 1400u;
}
/** @brief 验证转换停止错误仍能触发应用保护。 */
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)
{
    if (h == &hadc4) { s_vbus_stop_calls++; return s_vbus_stop; }
    return s_stop;
}
/** @brief 模拟单字符非阻塞命令输入。 */
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *h, uint8_t *out, uint16_t size, uint32_t timeout)
{
    (void)h;
    assert(size == 1u && timeout == 0u);
    if (s_measure_cycles != 0u) { test_dwt.CYCCNT += TEST_RX_CYCLES; }
    if (s_rx == 0u) { return HAL_TIMEOUT; }
    *out = s_rx;
    s_rx = 0u;
    return HAL_OK;
}

/** @brief 模拟 USART3 TX DMA：记录要发送的数据，由测试决定何时完成。 */
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *data, uint16_t size)
{
    assert(h == &huart3);
    if (s_measure_cycles != 0u) { test_dwt.CYCCNT += TEST_TX_CYCLES; }
    if (s_tx_dma_busy != 0u) { return HAL_BUSY; }
    assert(s_tx_length + (uint32_t)size <= sizeof(s_tx_capture));
    s_tx_dma_busy = 1u;
    memcpy(&s_tx_capture[s_tx_length], data, size);
    s_tx_length += (uint32_t)size;
    return HAL_OK;
}

/** @brief 模拟 DMA 完成：清忙标志并触发 HAL 的发送完成回调。 */
static void uart_dma_finish(void)
{
    s_tx_dma_busy = 0u;
    HAL_UART_TxCpltCallback(&huart3);
}

/** @brief 验证三相互补联锁、采样触发、占空比范围与紧急关断。 */
static void test_pwm(void)
{
    reset_hardware(); bsp_pwm_init(); bsp_pwm_start();
    assert(bsp_pwm_check_ready() == 1u && bsp_pwm_enable_bridge(1u) == 0u);
    bsp_pwm_set_duty(0.2f, 0.3f, 0.4f);
    test_pwm_update();
    assert(bsp_pwm_enable_bridge(1u) == 1u);
    test_tim1.BDTR = (test_tim1.BDTR & ~TIM_BDTR_DTG) | 1u;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    test_tim1.BDTR = (test_tim1.BDTR & ~TIM_BDTR_DTG) | 149u;
    test_tim1.CCER &= ~TIM_CCER_CC2NE;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    test_tim1.CCER |= TIM_CCER_CC2NE;
    test_tim1.CCR4 = 0u;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    test_tim1.CCR4 = 4165u;
    test_tim1.CR1 |= TIM_CR1_DIR;
    test_tim1.CNT = 4000u;
    assert(bsp_pwm_write_sampled(0.25f, 0.5f, 0.75f) == 1u);
    assert(test_pwm_active.ccr[0] == 850u);
    test_pwm_update();
    assert(test_pwm_active.ccr[0] == 1062u && test_pwm_active.ccr[2] == 3187u);
    assert(bsp_pwm_write_sampled(NAN, 0.5f, 0.5f) == 0u);
    test_tim1.CNT = 849u;
    assert(bsp_pwm_write_sampled(0.5f, 0.5f, 0.5f) == 0u);
    bsp_pwm_stop_emergency();
    assert(test_gpioc.BRR == CTRL_SD_Pin && bsp_pwm_is_bridge_enabled() == 0u);
    assert((test_tim1.BDTR & (TIM_BDTR_MOE | TIM_BDTR_AOE)) == 0u);
    assert((test_tim1.CCER & PWM_CCER_ALL) == 0u && (test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    bsp_pwm_start();
    assert((test_tim1.CR1 & TIM_CR1_DIR) == 0u && bsp_pwm_check_ready() == 1u);
}

/** @brief 模拟注入启动/停止，允许测试覆盖 HAL 失败清理。 */
HAL_StatusTypeDef HAL_ADCEx_InjectedStart(ADC_HandleTypeDef *h)
{ (void)h; return s_start; }
/** @brief 只有 ADC3 打开完成中断。 */
HAL_StatusTypeDef HAL_ADCEx_InjectedStart_IT(ADC_HandleTypeDef *h)
{ assert(h == &hadc3); h->Instance->IER |= ADC_IT_JEOS; return s_start; }
/** @brief 转换停止只能在关桥状态调用。 */
HAL_StatusTypeDef HAL_ADCEx_InjectedStop(ADC_HandleTypeDef *h)
{ (void)h; assert(bsp_pwm_is_bridge_enabled() == 0u); return s_stop; }
/** @brief 生产入口已消费 JEOS；HAL 只清理残余事件。 */
void HAL_ADC_IRQHandler(ADC_HandleTypeDef *h)
{ h->Instance->ISR = 0u; }
/** @brief 生产入口已消费 UIF。 */
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *h)
{ h->Instance->SR = 0u; }

/** @brief 一次真实 ADC 向量再一次更新向量，对应完整 50 us PWM 周期。 */
static void sample_cycle(uint16_t u, uint16_t v, uint16_t w)
{
    test_tim1.CR1 |= TIM_CR1_DIR;
    test_tim1.CNT = 4000u;
    s_adcs[0].JDR1 = u; s_adcs[1].JDR1 = v; s_adcs[2].JDR1 = w;
    for (uint32_t i = 0u; i < 3u; i++) { s_adcs[i].ISR = ADC_FLAG_JEOS | ADC_FLAG_JEOC; }
    ADC3_IRQHandler();
    test_tim1.SR |= TIM_SR_UIF;
    TIM1_UP_TIM16_IRQHandler();
    test_tim1.CR1 &= ~TIM_CR1_DIR;
    test_pwm_update();
}

/** @brief 校准期间必须关桥，完整窗口后才允许功率输出。 */
static void finish_calibration(void)
{
    for (uint32_t i = 0u; i < 127u; i++)
    {
        sample_cycle(1551u, 1550u, 1552u);
        assert(bsp_pwm_is_bridge_enabled() == 0u);
    }
    sample_cycle(1551u, 1550u, 1552u);
    assert(bsp_pwm_is_bridge_enabled() == 1u);
}

/** @brief 验证霍尔正反向步进及回绕，不推定真实电角度零点。 */
static void test_hall(void)
{
    const hall_state_t sequence[] = { HALL_S1, HALL_S3, HALL_S2, HALL_S6, HALL_S4, HALL_S5 };
    for (uint32_t i = 0u; i < 6u; i++)
    {
        float next = bsp_hall_get_sector_deg(sequence[(i + 1u) % 6u]);
        float delta = next - bsp_hall_get_sector_deg(sequence[i]);
        float reverse = bsp_hall_get_sector_deg(sequence[(i + 5u) % 6u]) - bsp_hall_get_sector_deg(sequence[i]);
        assert((delta == 60.0f) || (delta == -300.0f));
        assert((reverse == -60.0f) || (reverse == 300.0f));
        test_gpioc.IDR = (((uint32_t)sequence[i] >> 2u) & 1u) << 6u;
        test_gpioc.IDR |= (((uint32_t)sequence[i] >> 1u) & 1u) << 7u;
        test_gpioc.IDR |= ((uint32_t)sequence[i] & 1u) << 8u;
        assert(bsp_hall_read() == sequence[i]);
    }
    assert(bsp_hall_get_sector_deg((hall_state_t)99) == 0.0f);
    test_gpioc.IDR = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8;
    assert(bsp_hall_read() == HALL_INVALID);
}

/** @brief 验证 DMA 发送不阻塞、忙时不重复启动、队列回绕及满时整条丢弃。 */
static void test_log(void)
{
    reset_hardware(); bsp_uart_init(); app_log_init();

    app_log_write("AB");
    app_log_task();
    assert(s_tx_length == 2u && memcmp(s_tx_capture, "AB", 2u) == 0);
    // DMA 忙时再次调用不得启动第二次传输，也不得丢数据。
    app_log_task();
    assert(s_tx_length == 2u);
    uart_dma_finish();
    app_log_task();
    assert(s_tx_length == 2u);

    // 队列刚好装满 1023 字节（跨缓冲区末尾），随后一条 4 字节日志必须整条丢弃。
    for (uint32_t i = 0u; i < APP_LOG_QUEUE_SIZE - 1u; i++) { app_log_write("Y"); }
    app_log_write("DROP");
    s_tx_length = 0u;
    app_log_task();
    // 第一段只能发到缓冲区末尾；剩余 1 字节在下一轮发出。
    assert(s_tx_length == APP_LOG_QUEUE_SIZE - 2u);
    uart_dma_finish();
    app_log_task();
    assert(s_tx_length == APP_LOG_QUEUE_SIZE - 1u);
    for (uint32_t i = 0u; i < APP_LOG_QUEUE_SIZE - 1u; i++) { assert(s_tx_capture[i] == 'Y'); }
    uart_dma_finish();
    app_log_task();
    assert(s_tx_length == APP_LOG_QUEUE_SIZE - 1u);
}

/** @brief 发送失败释放在途段，接收错误与其他串口错误不释放发送缓冲区。 */
static void test_log_dma_error(void)
{
    UART_HandleTypeDef other_uart = {0};
    reset_hardware(); bsp_uart_init(); app_log_init();
    app_log_write("FAIL");
    app_log_task();
    app_log_write("NEXT");

    huart3.ErrorCode = HAL_UART_ERROR_ORE;
    HAL_UART_ErrorCallback(&huart3);
    assert(bsp_uart_is_tx_busy() == 1u);
    other_uart.ErrorCode = HAL_UART_ERROR_DMA;
    HAL_UART_ErrorCallback(&other_uart);
    assert(bsp_uart_is_tx_busy() == 1u);
    app_log_task();
    assert(s_tx_length == 4u);

    // HAL 在 DMA 错误回调前已终止发送；失败段不重试，下一段仍可发送。
    s_tx_dma_busy = 0u;
    huart3.ErrorCode = HAL_UART_ERROR_DMA;
    HAL_UART_ErrorCallback(&huart3);
    assert(bsp_uart_is_tx_busy() == 0u);
    app_log_task();
    assert(s_tx_length == 8u && memcmp(&s_tx_capture[4], "NEXT", 4u) == 0);
    assert(bsp_uart_is_tx_busy() == 1u);
    uart_dma_finish();
    app_log_task();
    assert(bsp_uart_is_tx_busy() == 0u);
}

/** @brief 验证串口启动、关桥校准、斜坡、停止与故障锁存。 */
static void test_application(void)
{
    app_control_feedback_t feedback;
    reset_hardware(); app_init();
    assert(app_get_mode() == APP_MODE_STOP && bsp_pwm_is_bridge_enabled() == 0u);
    assert((test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    s_rx = '1'; app_task();
    assert(app_get_mode() == APP_MODE_FOC && bsp_pwm_is_bridge_enabled() == 0u);
    finish_calibration();
    s_rx = '+'; app_task();
    sample_cycle(1551u, 1550u, 1552u);
    app_control_get_feedback(&feedback);
    assert(feedback.iq_ref > 0.0f && feedback.iq_ref <= 0.00011f);
    assert(app_control_set_angle(90.0f) == 0.0f);
    s_rx = '0'; app_task();
    assert(bsp_pwm_is_bridge_enabled() == 0u && (test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    assert(app_control_set_angle(90.0f) == 90.0f);
    s_rx = '1'; app_task(); finish_calibration();
    sample_cycle(2800u, 1550u, 1552u);
    assert(app_get_fault() == APP_FAULT_OVERCURRENT && bsp_pwm_is_bridge_enabled() == 0u);
    app_task(); s_rx = '1'; app_task();
    assert(app_get_mode() == APP_MODE_STOP && bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); s_calibration = HAL_ERROR; app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); s_vbus_calibration = HAL_ERROR; app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); app_init(); s_start = HAL_ERROR;
    s_rx = '1'; app_task(); assert(app_get_fault() == APP_FAULT_ADC);
}

/** @brief 采样丢失、半帧、晚到与原始值饱和均在 ISR 内关桥。 */
static void test_current_faults(void)
{
    for (uint32_t scenario = 0u; scenario < 5u; scenario++)
    {
        reset_hardware(); app_init(); s_rx = '1'; app_task(); finish_calibration();
        if (scenario == 0u)
        {
            test_tim1.SR |= TIM_SR_UIF;
            TIM1_UP_TIM16_IRQHandler();
            assert(app_get_fault() == APP_FAULT_SAMPLE_LOST);
        }
        else
        {
            test_tim1.CR1 |= TIM_CR1_DIR;
            test_tim1.CNT = (scenario == 2u) ? 800u : 4000u;
            for (uint32_t i = 0u; i < 3u; i++) { s_adcs[i].ISR = ADC_FLAG_JEOS; }
            if (scenario == 1u) { s_adcs[1].ISR = 0u; }
            if (scenario == 3u) { s_adcs[2].JDR1 = 4095u; }
            if (scenario == 4u) { s_tick += 21u; }
            ADC3_IRQHandler();
            assert(app_get_fault() != APP_FAULT_NONE);
        }
        assert(bsp_pwm_is_bridge_enabled() == 0u && test_gpioc.BRR == CTRL_SD_Pin);
        app_task(); s_rx = '1'; app_task();
        assert(app_get_mode() == APP_MODE_STOP && bsp_pwm_is_bridge_enabled() == 0u);
    }
}

/** @brief 零偏尖峰重建窗口，短暂共模干扰不锁死，持续失真仍关桥。 */
static void test_current_noise(void)
{
    reset_hardware(); app_init(); s_rx = '1'; app_task();
    sample_cycle(1551u, 1550u, 1552u);
    sample_cycle(1450u, 1450u, 1552u);
    assert(app_get_fault() == APP_FAULT_NONE && bsp_pwm_is_bridge_enabled() == 0u);
    finish_calibration();
    for (uint32_t i = 0u; i < 2u; i++)
    {
        sample_cycle(1450u, 1450u, 1552u);
        assert(app_get_fault() == APP_FAULT_NONE && bsp_pwm_is_bridge_enabled() == 1u);
        sample_cycle(1551u, 1550u, 1552u);
    }
    sample_cycle(1450u, 1450u, 1552u);
    sample_cycle(1450u, 1450u, 1552u);
    assert(app_get_fault() == APP_FAULT_NONE && bsp_pwm_is_bridge_enabled() == 1u);
    sample_cycle(1450u, 1450u, 1552u);
    assert(app_get_fault() == APP_FAULT_CURRENT_SUM && bsp_pwm_is_bridge_enabled() == 0u);
    app_task(); s_rx = '1'; app_task();
    assert(app_get_mode() == APP_MODE_STOP && bsp_pwm_is_bridge_enabled() == 0u);
}

/** @brief 在主循环关断序列中模拟最后一帧校准到达。 */
static void interrupt_shutdown(uint32_t *reg)
{
    (void)reg;
    test_pwm_after_write = NULL;
    ADC3_IRQHandler();
    assert(bsp_pwm_is_bridge_enabled() == 0u);
}

/** @brief 校准完成中断抢占主循环停机或故障时，绝不能重新开桥。 */
static void test_shutdown_race(void)
{
    for (uint32_t fault = 0u; fault < 2u; fault++)
    {
        reset_hardware(); app_init(); app_set_mode(APP_MODE_FOC);
        for (uint32_t i = 0u; i < 127u; i++) { sample_cycle(1551u, 1550u, 1552u); }
        for (uint32_t i = 0u; i < 3u; i++) { s_adcs[i].ISR = ADC_FLAG_JEOS; }
        test_pwm_after_write = interrupt_shutdown;
        if (fault != 0u) { app_control_trip_fault(APP_FAULT_BUS); }
        else { app_control_stop(); }
        assert(bsp_pwm_is_bridge_enabled() == 0u && (test_gpioc.ODR & CTRL_SD_Pin) == 0u);
        assert((test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    }
}

/** @brief 未核验板卡的应用命令与驱动接口均不能输出功率。 */
static void test_monitor_only(void)
{
    reset_hardware(); s_board_allow = 0u; app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    assert((test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    bsp_pwm_set_duty(0.5f, 0.5f, 0.5f); bsp_pwm_start();
    assert(bsp_pwm_check_ready() == 1u && bsp_pwm_enable_bridge(1u) == 0u);
}

/** @brief 实际调度的空接收不计工作，命令处理和 DMA 提交计工作且正确退出。 */
static void test_task_accounting(void)
{
    app_perf_snapshot_t snapshot;
    reset_hardware(); bsp_uart_init(); app_log_init(); app_perf_init();
    app_set_mode_raw(APP_MODE_STOP);
    s_measure_cycles = 1u;

    // 经过真实 app_task，能发现调用方误把空接收整体包进工作区间。
    app_task();
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.elapsed_cycles == TEST_RX_CYCLES + 100u);
    assert(snapshot.work_cycles == 0u);

    s_rx = 'h';
    app_cmd_task();
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.work_cycles == TEST_DELAY_CYCLES);
    assert(snapshot.task_cycles[APP_PERF_TASK_CMD] == TEST_DELAY_CYCLES);
    assert(snapshot.elapsed_cycles == TEST_RX_CYCLES + TEST_DELAY_CYCLES + 100u);

    app_log_task();
    test_dwt.CYCCNT += 100u;
    app_log_task();  /* DMA 等待，不能重复提交。 */
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.work_cycles == TEST_TX_CYCLES);
    assert(snapshot.task_cycles[APP_PERF_TASK_LOG] == TEST_TX_CYCLES);
    assert(snapshot.elapsed_cycles == TEST_TX_CYCLES + 100u);

    uart_dma_finish();
    app_log_task();  /* 仅回收，没有下一段；必须退出计时。 */
    app_log_task();  /* 空队列。 */
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.work_cycles == 0u);

    app_log_write("retry");
    s_tx_dma_busy = 1u;  /* HAL 拒绝提交，但 BSP 忙标志仍为零。 */
    app_log_task();
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.work_cycles == TEST_TX_CYCLES);
    s_tx_dma_busy = 0u;
    app_log_task();
    uart_dma_finish();
    app_log_task();
    test_dwt.CYCCNT += 100u;
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.work_cycles == TEST_TX_CYCLES);
}

/** @brief 调用真实向量函数，验证 DMA/USART/SysTick 覆盖、嵌套并集和退出边界。 */
static void test_irq_accounting(void)
{
    app_perf_snapshot_t snapshot;
    reset_hardware(); app_perf_init();
    test_dwt.CYCCNT += 17u;
    DMA1_Channel2_IRQHandler();
    USART3_IRQHandler();
    test_dwt.CYCCNT += 23u;
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.irq_cycles == 50u && snapshot.work_cycles == 50u);
    assert(snapshot.irq_by_source[APP_PERF_IRQ_DMA] == 24u);
    assert(snapshot.irq_by_source[APP_PERF_IRQ_SYSTICK] == 7u);
    assert(snapshot.irq_by_source[APP_PERF_IRQ_UART] == 19u);
    assert(snapshot.elapsed_cycles == 90u && test_primask == 0u);
}

/** @brief 通过实际环形队列与 DMA 桩排空日志，覆盖跨队尾的分段发送。 */
static void drain_log(void)
{
    for (uint32_t i = 0u; i < 4u; i++)
    {
        app_log_task();
        if (s_tx_dma_busy != 0u) { uart_dma_finish(); }
    }
    assert(s_tx_dma_busy == 0u && s_tx_length < sizeof(s_tx_capture));
    s_tx_capture[s_tx_length] = '\0';
}

/** @brief 空闲时仅更新统计；跨毫秒回绕后由单字符命令读取最近一秒的报告。 */
static void test_requested_report(void)
{
    uint32_t base, length, tick;
    reset_hardware();
    s_tick = UINT32_MAX - 500u;
    app_init();
    tick = s_tick;
    base = test_dwt.CYCCNT;
    drain_log();
    s_tx_length = 0u;
    for (uint32_t window = 1u; window <= 2u; window++)
    {
        s_tick = tick + window * 1000u - 1u;
        test_dwt.CYCCNT = base + (SystemCoreClock / 1000u) * (window * 1000u - 1u);
        app_task(); drain_log();
        assert(s_tx_length == 0u);
        s_tick++;
        test_dwt.CYCCNT += SystemCoreClock / 1000u;
        app_task(); drain_log();
        assert(s_tx_length == 0u);
        s_rx = 'p'; app_task(); drain_log();
        assert(strstr(s_tx_capture, "[FOC]") == NULL);
        assert(strstr(s_tx_capture, "[CPU] TASK ") != NULL);
        assert(strstr(s_tx_capture, "IRQ         CPU") != NULL);
        assert(strstr(s_tx_capture, "CONTROL") != NULL && strstr(s_tx_capture, "TIM1") != NULL);
        assert(strstr(s_tx_capture, "SYSTICK") != NULL && strstr(s_tx_capture, "USART3") != NULL);
        assert(strstr(s_tx_capture, "DMA_TX") != NULL && strstr(s_tx_capture, "PERF") != NULL);
        assert(strstr(s_tx_capture, "CURRENT  OFF\r\n") != NULL);
        assert(strstr(s_tx_capture, "raw=") == NULL && strstr(s_tx_capture, "VBUS") == NULL);
        assert(strstr(s_tx_capture, "VREF") == NULL);
        assert(strstr(strstr(s_tx_capture, "[CPU]") + 1, "[CPU]") == NULL);
        length = s_tx_length;
        app_task(); app_task(); drain_log();
        assert(s_tx_length == length);
        s_tx_length = 0u;
    }
}

/** @brief 在实际 DMA 字节流中核对六个通道、无效值和固定帧尾。 */
static void check_vofa_frame(uint32_t offset, const app_control_feedback_t *feedback)
{
    const uint8_t tail[] = {0x00u, 0x00u, 0x80u, 0x7fu};
    float values[6];
    assert(s_tx_length >= offset + sizeof(values) + sizeof(tail));
    memcpy(values, &s_tx_capture[offset], sizeof(values));
    assert(memcmp(&s_tx_capture[offset + sizeof(values)], tail, sizeof(tail)) == 0);
    if (feedback == NULL)
    {
        assert(isnan(values[0]) && isnan(values[1]) && isnan(values[2]));
    }
    else
    {
        assert(values[0] == feedback->foc.iq && values[1] == feedback->foc.id);
        assert(values[2] == feedback->iq_ref);
    }
    assert(values[3] == app_control_get_angle());
    assert(isnan(values[4]) && isnan(values[5]));
}

/** @brief 检查开关、50 Hz 节拍、跳帧、命令与故障切回文本。 */
static void test_vofa_stream(void)
{
    const uint8_t tail[] = {0x00u, 0x00u, 0x80u, 0x7fu};
    app_control_feedback_t feedback;
    uint32_t length;
    reset_hardware();
    s_tick = UINT32_MAX - 10u;
    app_init(); drain_log(); s_tx_length = 0u;
    s_rx = 'v'; app_task();
    s_rx = '>'; app_task(); drain_log();
    assert(s_tx_length == 0u && app_control_get_angle() == 15.0f);
    s_tick += 19u; app_task(); drain_log();
    assert(s_tx_length == 0u);
    s_tick++; app_task(); drain_log();
    assert(s_tx_length == 32u && memcmp(s_tx_capture, tail, sizeof(tail)) == 0);
    check_vofa_frame(4u, NULL);
    s_tick += 61u; app_task(); app_task(); drain_log();
    assert(s_tx_length == 60u);
    check_vofa_frame(32u, NULL);
    s_tx_length = 0u;
    for (uint32_t i = 0u; i < 50u; i++)
    {
        s_tick += 20u; app_task(); drain_log();
        assert(s_tx_length == (i + 1u) * 28u);
        check_vofa_frame(i * 28u, NULL);
    }
    s_rx = 'v'; app_task(); s_tick += 20u; app_task(); drain_log();
    assert(s_tx_length == 50u * 28u);

    s_rx = '1'; app_task(); finish_calibration(); drain_log();
    s_tx_length = 0u;
    s_rx = 'v'; app_task();
    s_rx = '+'; app_task();
    s_tick += 20u; app_control_task();
    sample_cycle(1554u, 1548u, 1551u);
    app_control_get_feedback(&feedback);
    assert(feedback.running != 0u && feedback.samples > 0u && feedback.iq_ref > 0.0f);
    app_task(); drain_log();
    assert(s_tx_length == 32u);
    check_vofa_frame(4u, &feedback);

    // DMA 占用时不覆盖缓冲，不补发积压帧。
    s_tick += 20u; app_task(); app_log_task();
    assert(s_tx_dma_busy != 0u && s_tx_length == 60u);
    s_tick += 20u; app_task();
    assert(s_tx_length == 60u);
    uart_dma_finish(); drain_log();
    assert(s_tx_length == 60u);
    s_rx = 'p'; app_task(); drain_log();
    assert(strstr(&s_tx_capture[60], "[CPU]") != NULL);
    length = s_tx_length;
    s_tick += 20u; app_task(); drain_log();
    assert(s_tx_length == length);

    s_rx = 'v'; app_task(); app_control_trip_fault(APP_FAULT_OVERCURRENT);
    app_task(); drain_log();
    assert(strstr(&s_tx_capture[length], "[FAULT] code=2") != NULL);
    s_tick += 40u; length = s_tx_length; app_task(); drain_log();
    assert(s_tx_length == length && bsp_pwm_is_bridge_enabled() == 0u);
}

/** @brief 第二通道失败或采样过期时，实际应用必须关桥并锁住启动入口。 */
static void test_reference_faults(void)
{
    for (uint32_t scenario = 0u; scenario < 2u; scenario++)
    {
        reset_hardware(); app_set_mode_raw(APP_MODE_STOP); app_init();
        app_set_mode(APP_MODE_FOC);
        finish_calibration();
        if (scenario == 0u) { s_reference_poll = HAL_TIMEOUT; }
        else { s_reference_delay_ms = 21u; }
        s_tick += 10u; app_task(); app_task();
        assert(app_get_fault() == APP_FAULT_BUS && app_get_mode() == APP_MODE_STOP);
        assert(bsp_pwm_is_bridge_enabled() == 0u);
        s_reference_poll = HAL_OK; s_reference_delay_ms = 0u;
        app_set_mode(APP_MODE_FOC);
        assert(app_get_mode() == APP_MODE_STOP && bsp_pwm_is_bridge_enabled() == 0u);
    }
}

/** @brief 回归测试入口。 */
int32_t main(void)
{
    test_pwm(); test_hall(); test_log(); test_log_dma_error(); test_application(); test_monitor_only();
    test_task_accounting(); test_irq_accounting(); test_requested_report(); test_vofa_stream();
    test_current_faults(); test_current_noise(); test_shutdown_race(); test_reference_faults();
    puts("PASS: G474 PWM interlocks, Hall, log queue, application startup/fault latch");
    return 0;
}
