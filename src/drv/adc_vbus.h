/**
 * @file    adc_vbus.h
 * @brief   母线电压监视公共接口，ADC 与引脚由平台确定
 */
#ifndef ADC_VBUS_H
#define ADC_VBUS_H

#include <stdint.h>

typedef struct
{
    uint16_t raw;       /* 12 位 ADC 原始值 */
    float voltage;     /* 分压还原后的母线电压，单位 V */
} adc_vbus_sample_t;

/**
 * @brief  校准 CubeMX 已配置的母线 ADC，须在关桥状态下调用
 * @note   ADC 句柄由平台管理；本模块只使用规则组单次软件触发
 * @retval 1 成功；0 失败，禁止启动功率级
 */
uint8_t adc_vbus_init(void);

/**
 * @brief  单次读取母线电压，仅供主循环低频调用
 * @param  out: 输出样本；失败时保持原值，调用方不得使用旧样本
 * @retval 1 成功；0 失败，调用方应关桥并锁存故障
 * @note   转换轮询超时 1 ms，不得从 ISR 调用；不提供过欠压保护。
 */
uint8_t adc_vbus_read(adc_vbus_sample_t *out);

#endif /* ADC_VBUS_H */
