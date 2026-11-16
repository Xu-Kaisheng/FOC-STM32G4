/**
 * @file    bsp_uart.h
 * @brief   调试串口（USART3, PB10/PB11, 115200 8N1）
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

/**
 * @brief  初始化调试串口队列（CubeMX 已初始化 huart3，这里清状态）
 */
void bsp_uart_init(void);

/**
 * @brief  主循环后台发送任务，每次最多发送一个字节，不等待硬件
 */
void bsp_uart_task(void);

/**
 * @brief  非阻塞读取一个命令字节
 * @param  out: 字节输出地址
 * @retval 1 收到；0 无数据或参数无效
 */
uint8_t bsp_uart_read(uint8_t *out);

/**
 * @brief  格式化日志入队，发送由 bsp_uart_task 推进
 * @param  fmt: printf 风格格式串
 * @note   禁止在中断里调用；单条最多 127 字节，队列满时丢弃整条日志。
 */
void bsp_uart_printf(const char *fmt, ...);

#endif /* BSP_UART_H */
