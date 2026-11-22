/* 主机回归测试替身：仅模拟软件可见寄存器和 HAL 返回值，不替代硬件验证。 */
#ifndef TEST_STM32_HAL_H
#define TEST_STM32_HAL_H
#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t IDR, ODR, BRR; } GPIO_TypeDef;
typedef struct { uint32_t CR1, CR2, CCER, BDTR, ARR, PSC, RCR, CCMR1, CCMR2, CNT, EGR, CCR1, CCR2, CCR3; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
typedef struct { uint32_t DR; } ADC_TypeDef;
typedef struct { ADC_TypeDef *Instance; } ADC_HandleTypeDef;
typedef struct { uint32_t ISR, TDR; } USART_TypeDef;
typedef struct { USART_TypeDef *Instance; uint32_t ErrorCode; } UART_HandleTypeDef;
typedef struct { uint32_t unused; } DMA_HandleTypeDef;

extern GPIO_TypeDef test_gpioc;
extern TIM_TypeDef test_tim1;
#define GPIOC (&test_gpioc)
#define TIM1 (&test_tim1)
#define GPIO_PIN_6 (1u << 6)
#define GPIO_PIN_7 (1u << 7)
#define GPIO_PIN_8 (1u << 8)
#define GPIO_PIN_9 (1u << 9)
#define TIM_CHANNEL_1 0u
#define TIM_CHANNEL_2 4u
#define TIM_CHANNEL_3 8u
#define TIM_CR1_CEN 1u
#define TIM_CR1_UDIS 2u
#define TIM_CR2_CCPC 1u
#define TIM_CR2_CCUS 4u
#define TIM_CR2_OIS1 0x100u
#define TIM_CR2_OIS1N 0x200u
#define TIM_CR2_OIS2 0x400u
#define TIM_CR2_OIS2N 0x800u
#define TIM_CR2_OIS3 0x1000u
#define TIM_CR2_OIS3N 0x2000u
#define TIM_CR1_CMS 0x60u
#define TIM_CR1_CKD 0x300u
#define TIM_COUNTERMODE_CENTERALIGNED1 0x20u
#define TIM_CCER_CC1E 0x001u
#define TIM_CCER_CC1P 0x002u
#define TIM_CCER_CC1NE 0x004u
#define TIM_CCER_CC1NP 0x008u
#define TIM_CCER_CC2E 0x010u
#define TIM_CCER_CC2P 0x020u
#define TIM_CCER_CC2NE 0x040u
#define TIM_CCER_CC2NP 0x080u
#define TIM_CCER_CC3E 0x100u
#define TIM_CCER_CC3P 0x200u
#define TIM_CCER_CC3NE 0x400u
#define TIM_CCER_CC3NP 0x800u
#define TIM_BDTR_DTG 0xffu
#define TIM_BDTR_OSSI 0x400u
#define TIM_BDTR_OSSR 0x800u
#define TIM_BDTR_AOE 0x4000u
#define TIM_BDTR_MOE 0x8000u
#define TIM_CCMR1_CC1S 3u
#define TIM_CCMR1_CC2S 0x300u
#define TIM_CCMR1_OC1M 0x10070u
#define TIM_CCMR1_OC2M 0x1007000u
#define TIM_CCMR1_OC1PE 8u
#define TIM_CCMR1_OC2PE 0x800u
#define TIM_CCMR2_CC3S 3u
#define TIM_CCMR2_OC3M 0x10070u
#define TIM_CCMR2_OC3PE 8u
#define TIM_OCMODE_PWM1 0x60u
#define TIM_OCMODE_FORCED_INACTIVE 0x40u
#define TIM_EGR_UG 1u
#define TIM_EGR_COMG 0x20u
#define WRITE_REG(reg, value) test_pwm_write_reg(&(reg), (value))
#define MODIFY_REG(reg, mask, value) WRITE_REG((reg), (((reg) & ~(mask)) | (value)))
#define SET_BIT(reg, mask) WRITE_REG((reg), ((reg) | (mask)))
#define CLEAR_BIT(reg, mask) WRITE_REG((reg), ((reg) & ~(mask)))
#define __HAL_TIM_ENABLE(h) ((h)->Instance->CR1 |= TIM_CR1_CEN)
#define __HAL_TIM_DISABLE(h) ((h)->Instance->CR1 &= ~TIM_CR1_CEN)
#define __HAL_TIM_SET_COMPARE(h,c,v) test_set_compare((h),(c),(v))
#define __DSB() ((void)0)
extern uint16_t test_vref_cal;
#define VREFINT_CAL_ADDR (&test_vref_cal)
#define ADC_RESOLUTION_12B 0u
#define __HAL_ADC_CALC_VREFANALOG_VOLTAGE(raw, resolution) ((uint32_t)test_vref_cal * 3000u / (raw))
#define ADC_SINGLE_ENDED 0x7fu
#define DISABLE 0u
#define RESET 0u
#define UART_FLAG_TXE 0x80u
#define UART_FLAG_ORE 0x08u
#define HAL_UART_ERROR_ORE 0x08u
#define HAL_UART_ERROR_DMA 0x10u
#define __HAL_UART_GET_FLAG(h,f) ((h)->Instance->ISR & (f))
#define __HAL_UART_CLEAR_OREFLAG(h) ((h)->Instance->ISR &= ~UART_FLAG_ORE)

void test_set_compare(TIM_HandleTypeDef *h, uint32_t channel, uint32_t value);
void test_pwm_write_reg(uint32_t *reg, uint32_t value);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay);
void HAL_IncTick(void);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h);
void HAL_UART_IRQHandler(UART_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t mode);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *h, uint8_t *out, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *data, uint16_t size);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h);
#endif
