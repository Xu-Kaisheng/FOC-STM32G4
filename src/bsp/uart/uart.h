/**
 * @file    uart.h
 * @brief   调试串口硬件接口（USART3, PB10/PB11, 115200 8N1）
 *
 * 本模块只管硬件：初始化、启动一次 DMA 发送、查询发送忙、读一个字节。
 * 日志格式化、队列和"每轮主循环推进"属于应用层，见 `app/log`。
 *
 * 硬件配置（CubeMX）：DMA1_Channel2、DMAMUX 请求 USART3_TX、NORMAL 模式；
 * 使能 DMA1_Channel2 与 USART3 两个全局中断（HAL 在 DMA 完成后靠 USART
 * 的 TC 中断结束发送）。
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

/* ---------------- init ---------------- */

/**
 * @brief  初始化硬件发送状态，清除接收溢出标志
 */
void bsp_uart_init(void);

/* ---------------- read ---------------- */

/**
 * @brief  非阻塞读取一个字节，同时恢复接收溢出状态
 * @param  out: 字节输出地址
 * @retval 1 收到；0 无数据或参数无效
 */
uint8_t bsp_uart_read_byte(uint8_t *out);

/**
 * @brief  查询 DMA 是否仍在发送
 * @retval 1 忙；0 空闲
 */
uint8_t bsp_uart_is_tx_busy(void);

/* ---------------- write ---------------- */

/**
 * @brief  启动一次 DMA 发送
 * @param  data: 数据地址；调用期间必须保持有效（静态缓冲）
 * @param  len: 字节数，1~65535
 * @retval 1 已启动；0 忙、参数无效或 HAL 拒绝
 * @note   不拷贝数据、不阻塞等待；完成由 USART3 TC 中断回调通知。
 */
uint8_t bsp_uart_write(const uint8_t *data, uint32_t len);

#endif /* UART_H */
