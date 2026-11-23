/**
 * @file    adc_cur.h
 * @brief   三相电流接口（G474：PC0/ADC1、PC1/ADC2、PB1/ADC3）
 *
 * 采样链路（ATK-PD6010B）：
 *   相电流 I -> 20 mΩ 采样电阻 -> 差分放大 x6 -> +1.25 V 偏置 -> RC 滤波
 *   因此   V = 1.25 + 0.12 x I   (1 A 对应 120 mV)
 *
 * 三个独立 ADC 由 TIM1_TRGO2 同时触发；ADC3 完成中断读取完整样本。
 * 每次启动先在关桥状态采集零偏，参考电压在启动时固定。
 */
#ifndef ADC_CUR_H
#define ADC_CUR_H

#include <stdint.h>

/* ---------------- 公共宏 ---------------- */

/* 换算常数 */
#define ADC_CUR_OFFSET_V    1.25f            /* 0 A 对应的电压 */
#define ADC_CUR_GAIN_V_PER_A 0.12f           /* 1 A 产生的电压 */

/* ---------------- 公共类型 ---------------- */

typedef struct
{
    float    iu;        /* U 相电流 A */
    float    iv;        /* V 相电流 A */
    float    iw;        /* W 相电流 A */
    uint16_t raw[3];    /* 原始 ADC 值，调试用 */
} adc_cur_sample_t;

/* ---------------- init ---------------- */

/**
 * @brief  初始化 ADC 电流采样：校准并准备平台采样通道
 * @note   ADC 句柄和通道由各平台实现管理。
 * @retval 1 成功；0 失败，须保持关桥并复位后重试
 */
uint8_t bsp_adc_cur_init(void);

/* ---------------- read ---------------- */

/**
 * @brief  读取本周期完整注入样本并推进关桥零偏标定
 * @param  out: 输出结构体
 * @retval 1 成功；0 失败，out 保持原值，不能当作有效电流使用
 * @note   仅 ADC3 ISR 调用，无等待；标定完成前不得开桥。
 */
uint8_t bsp_adc_cur_read(adc_cur_sample_t *out);

/** @brief 查询本次关桥零偏标定是否完成，仅控制 ISR 使用。 */
uint8_t bsp_adc_cur_is_calibrated(void);

/* ---------------- write ---------------- */

/** @brief 定时器停止时准备三路注入转换，vref_mv 为本次启动参考电压。 */
uint8_t bsp_adc_cur_start(uint16_t vref_mv);
/** @brief 主循环关桥停定时器后停止转换；返回 0 时须复位。 */
uint8_t bsp_adc_cur_stop(void);

#endif /* ADC_CUR_H */
