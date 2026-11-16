/**
 * @file    adc_cur.h
 * @brief   三相电流接口（G474：PC0/ADC1、PC1/ADC2、PB1/ADC3）
 *
 * 采样链路（ATK-PD6010B）：
 *   相电流 I -> 20 mΩ 采样电阻 -> 差分放大 x6 -> +1.25 V 偏置 -> RC 滤波
 *   因此   V = 1.25 + 0.12 x I   (1 A 对应 120 mV)
 *
 * G474 的关桥诊断实现使用三个独立 ADC 依次软件触发，详见 docs/11。
 * v0.1 用软件触发按需采样；v0.2 改成 TIM1_TRGO 硬件触发、在 20 kHz 中断里读。
 */
#ifndef ADC_CUR_H
#define ADC_CUR_H

#include <stdint.h>

/* 换算常数 */
#define ADC_CUR_VREF        3.3f             /* VDDA，后续用 VREFINT 校准 */
#define ADC_CUR_FULL_SCALE  4095.0f
#define ADC_CUR_OFFSET_V    1.25f            /* 0 A 对应的电压 */
#define ADC_CUR_GAIN_V_PER_A 0.12f           /* 1 A 产生的电压 */

typedef struct
{
    float    iu;        /* U 相电流 A */
    float    iv;        /* V 相电流 A */
    float    iw;        /* W 相电流 A */
    uint16_t raw[3];    /* 原始 ADC 值，调试用 */
} adc_cur_sample_t;

/**
 * @brief  初始化 ADC 电流采样：校准并准备平台采样通道
 * @note   ADC 句柄和通道由各平台实现管理。
 * @retval 1 成功；0 失败，须保持关桥并复位后重试
 */
uint8_t adc_cur_init(void);

/**
 * @brief  阻塞式读取一次三相电流（软件触发；时序由平台实现决定）
 * @param  out: 输出结构体
 * @retval 1 成功；0 失败，out 保持原值，不能当作有效电流使用
 * @note   v0.1 专用；v0.2 换成 TIM1 触发的注入中断，避免占用主循环
 */
uint8_t adc_cur_read(adc_cur_sample_t *out);

#endif /* ADC_CUR_H */
