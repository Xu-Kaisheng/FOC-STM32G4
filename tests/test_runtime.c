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

/* 可控执行耗时只用于核对统计区间，不代表实板测量。 */
#define TEST_RX_CYCLES    13u
#define TEST_TX_CYCLES    29u
#define TEST_DELAY_CYCLES 37u

GPIO_TypeDef test_gpioc;
TIM_TypeDef test_tim1;
static ADC_TypeDef s_adcs[4];
static USART_TypeDef s_uart;
static char s_tx_capture[1024];
static uint32_t s_tx_length;
static uint8_t s_tx_dma_busy;
ADC_HandleTypeDef hadc1, hadc2, hadc3, hadc4;
TIM_HandleTypeDef htim1;
UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_tx;
static uint8_t s_measure_cycles;
static uint32_t s_tick;
static uint8_t s_rx;
static uint8_t s_board_allow = 1u;
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
    test_tim1.BDTR = 149u | TIM_BDTR_OSSI | TIM_BDTR_OSSR | TIM_BDTR_AOE;
    test_tim1.CCMR1 = TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << 8u) | TIM_CCMR1_OC1PE | TIM_CCMR1_OC2PE;
    test_tim1.CCMR2 = TIM_OCMODE_PWM1 | TIM_CCMR2_OC3PE;
    s_uart.ISR = 0u;
    s_tx_length = 0u;
    s_tx_dma_busy = 0u;
    s_calibration = s_start = s_poll = s_stop = HAL_OK;
    s_vbus_calibration = s_vbus_start = s_vbus_poll = s_vbus_stop = HAL_OK;
    s_vbus_raw = 1192u;
    s_vbus_stop_calls = 0u;
    s_tick = 0u;
    s_rx = 0u;
    s_board_allow = 1u;
    s_verify_boot = 0u;
    s_boot_pwm_ticks = 0u;
    s_measure_cycles = 0u;
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
    return s_tick++;
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
/** @brief 诊断板禁用上电自检，测试驱动许可开启时覆盖原有自检路径。 */
uint8_t bsp_board_get_boot_test(void) { return s_board_allow; }
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
{ return (h == &hadc4) ? s_vbus_start : s_start; }
/** @brief 所有诊断 ADC 的主循环轮询均有 1 ms 超时参数。 */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout)
{ assert(timeout == 1u); return (h == &hadc4) ? s_vbus_poll : s_poll; }
/** @brief 提供三相测试码及独立母线测试码。 */
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h)
{
    if (h == &hadc4) { return s_vbus_raw; }
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

/** @brief 验证六步合法组合和异常配置均不能绕过联锁。 */
static void test_pwm(void)
{
    for (uint32_t high = 0u; high < 3u; high++)
    {
        for (uint32_t low = 0u; low < 3u; low++)
        {
            if (high == low) { continue; }
            reset_hardware(); bsp_pwm_init(); bsp_pwm_start();
            assert(bsp_pwm_check_ready() == 1u);
            bsp_pwm_enable_phase((pwm_phase_t)(3u - high - low), 0u);
            assert(bsp_pwm_check_ready() == 0u && bsp_pwm_enable_bridge(1u) == 0u);
            assert(bsp_pwm_enable_bridge_sixstep((pwm_phase_t)high, (pwm_phase_t)low) == 1u);
            assert(bsp_pwm_is_bridge_enabled() == 1u && test_tim1.EGR == TIM_EGR_UG);
            test_tim1.CCER &= ~(TIM_CCER_CC1NE << (high * 4u));
            assert(bsp_pwm_enable_bridge_sixstep((pwm_phase_t)high, (pwm_phase_t)low) == 0u);
            assert((test_gpioc.ODR & CTRL_SD_Pin) == 0u);
        }
    }
    reset_hardware(); bsp_pwm_init(); bsp_pwm_start();
    assert(bsp_pwm_enable_bridge_sixstep(PHASE_U, PHASE_V) == 0u);
    test_tim1.BDTR = (test_tim1.BDTR & ~TIM_BDTR_DTG) | 1u;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    test_tim1.BDTR = (test_tim1.BDTR & ~TIM_BDTR_DTG) | 149u;
    test_tim1.CCER |= TIM_CCER_CC1P;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    test_tim1.CCER &= ~TIM_CCER_CC1P;
    test_tim1.PSC = 1u;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    test_tim1.PSC = 0u;
    test_tim1.CCMR1 &= ~TIM_CCMR1_OC1PE;
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    assert(bsp_pwm_enable_bridge_sixstep(PHASE_U, PHASE_U) == 0u);
    assert(bsp_pwm_enable_bridge_sixstep((pwm_phase_t)99, PHASE_V) == 0u);
    reset_hardware(); bsp_pwm_init(); bsp_pwm_start();
    assert(bsp_pwm_enable_bridge(1u) == 1u);
    bsp_pwm_stop_emergency();
    assert(test_gpioc.BRR == CTRL_SD_Pin && bsp_pwm_is_bridge_enabled() == 0u);
    assert((test_tim1.BDTR & (TIM_BDTR_MOE | TIM_BDTR_AOE)) == 0u);
    assert((test_tim1.CCER & 0x555u) == 0u && (test_tim1.CR1 & TIM_CR1_CEN) == 0u);
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

    // 队列刚好装满 511 字节（跨缓冲区末尾），随后一条 4 字节日志必须整条丢弃。
    for (uint32_t i = 0u; i < 511u; i++) { app_log_write("Y"); }
    app_log_write("DROP");
    s_tx_length = 0u;
    app_log_task();
    // 第一段只能发到缓冲区末尾；剩余 1 字节在下一轮发出。
    assert(s_tx_length == 510u);
    uart_dma_finish();
    app_log_task();
    assert(s_tx_length == 511u);
    for (uint32_t i = 0u; i < 511u; i++) { assert(s_tx_capture[i] == 'Y'); }
    uart_dma_finish();
    app_log_task();
    assert(s_tx_length == 511u);
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

/** @brief 验证实际应用启动、反转、停机及 ADC 故障锁存。 */
static void test_application(void)
{
    reset_hardware(); app_init();
    assert(bsp_pwm_is_bridge_enabled() == 0u);
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 1u);
    s_rx = '2'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 1u);
    s_rx = '0'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 1u);
    s_poll = HAL_TIMEOUT; s_tick += 1000u; app_task();
    assert(bsp_pwm_is_bridge_enabled() == 0u);
    s_poll = HAL_OK; s_rx = '1'; app_task();
    assert(bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); s_calibration = HAL_ERROR; app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); s_vbus_calibration = HAL_ERROR; app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); s_vbus_start = HAL_ERROR; app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    reset_hardware(); app_init();
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 1u);
    s_vbus_poll = HAL_TIMEOUT; s_tick += 1000u; app_task();
    assert(bsp_pwm_is_bridge_enabled() == 0u);
    s_vbus_poll = HAL_OK; s_rx = '1'; app_task();
    assert(bsp_pwm_is_bridge_enabled() == 0u);
}

/** @brief 未核验板卡的应用命令与驱动接口均不能输出功率。 */
static void test_monitor_only(void)
{
    reset_hardware(); s_board_allow = 0u; s_verify_boot = 1u; app_init();
    s_verify_boot = 0u;
    assert(s_boot_pwm_ticks == 0u);
    assert((test_tim1.BDTR & TIM_BDTR_MOE) == 0u && test_tim1.CCER == 0u);
    assert(test_tim1.CCR1 == 0u && test_tim1.CCR2 == 0u && test_tim1.CCR3 == 0u);
    assert((test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    s_rx = '1'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    assert((test_tim1.CR1 & TIM_CR1_CEN) == 0u);
    s_rx = '2'; app_task(); assert(bsp_pwm_is_bridge_enabled() == 0u);
    bsp_pwm_start();
    assert(bsp_pwm_check_ready() == 1u);
    assert(bsp_pwm_enable_bridge(1u) == 0u);
    bsp_pwm_enable_phase(PHASE_W, 0u);
    assert(bsp_pwm_enable_bridge_sixstep(PHASE_U, PHASE_V) == 0u);
    assert((test_gpioc.ODR & CTRL_SD_Pin) == 0u);
}

/** @brief 实际调度的空接收不计工作，命令处理和 DMA 提交计工作且正确退出。 */
static void test_task_accounting(void)
{
    app_perf_snapshot_t snapshot;
    reset_hardware(); bsp_uart_init(); app_log_init(); app_perf_init();
    app_set_mode_raw(APP_MODE_STOP);
    app_set_last_print_ms(s_tick);
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
    assert(snapshot.elapsed_cycles == TEST_RX_CYCLES + TEST_DELAY_CYCLES + 100u);

    app_log_task();
    test_dwt.CYCCNT += 100u;
    app_log_task();  /* DMA 等待，不能重复提交。 */
    assert(app_perf_get_snapshot(&snapshot) == 1u);
    assert(snapshot.work_cycles == TEST_TX_CYCLES);
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
    assert(snapshot.elapsed_cycles == 90u && test_primask == 0u);
}

/** @brief 回归测试入口。 */
int32_t main(void)
{
    test_pwm(); test_hall(); test_log(); test_log_dma_error(); test_application(); test_monitor_only();
    test_task_accounting(); test_irq_accounting();
    puts("PASS: G474 PWM interlocks, Hall, log queue, application startup/fault latch");
    return 0;
}
