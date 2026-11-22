/**
 * @file    adc_vbus.h
 * @brief   母线电压监视公共接口，ADC 与引脚由平台确定
 */
#ifndef ADC_VBUS_H
#define ADC_VBUS_H

#include <stdint.h>

/* ---------------- 公共类型 ---------------- */

typedef struct
{
    uint16_t vref_mv;   /* 本轮 VREFINT 推算的 VREF+，单位 mV */
    uint32_t sampled_ms; /* 本轮采样起始时间，用于拒绝过期换算 */
    uint16_t raw;       /* 12 位 ADC 原始值 */
    float voltage;     /* 分压还原后的母线电压，单位 V */
} adc_vbus_sample_t;

/* ---------------- init ---------------- */

/**
 * @brief  校准 CubeMX 已配置的母线 ADC，须在关桥状态下调用
 * @note   ADC 句柄由平台管理；规则组依次采集 VBUS、VREFINT，自动等待读取后再推进
 * @retval 1 成功；0 失败，禁止启动功率级
 */
uint8_t bsp_adc_vbus_init(void);

/* ---------------- read ---------------- */

/**
 * @brief  读取 16 组 VBUS / VREFINT 平均值，仅供主循环低频调用
 * @param  out: 输出样本；失败时保持原值，调用方不得使用旧样本
 * @retval 1 成功；0 失败，调用方应关桥并锁存故障
 * @note   转换轮询超时 1 ms，不得从 ISR 调用；不提供过欠压保护。
 */
uint8_t bsp_adc_vbus_read(adc_vbus_sample_t *out);

#endif /* ADC_VBUS_H */
