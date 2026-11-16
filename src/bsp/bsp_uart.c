/**
 * @file    bsp_uart.c
 * @brief   调试串口实现：把 printf 风格的日志送到 USART3
 */
#include "bsp_uart.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>

/* 一次日志的最大长度。128 字节够打一行状态；静态存储避免占用栈。 */
#define UART_LOG_BUF_SIZE  128u
#define UART_TX_QUEUE_SIZE 512u

/* 日志生产和发送都在主循环，队列无须关中断；满时整条丢弃，不能阻塞换相。 */
static char s_log_buf[UART_LOG_BUF_SIZE];
static uint8_t s_tx_queue[UART_TX_QUEUE_SIZE];
static uint32_t s_tx_head = 0u;
static uint32_t s_tx_tail = 0u;

/**
 * @brief  初始化发送队列并清除串口溢出标志
 * @retval 无
 */
void bsp_uart_init(void)
{
    // 清掉可能残留的错误标志，否则第一次发送会直接返回 HAL_ERROR
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    s_tx_head = 0u;
    s_tx_tail = 0u;
}

/**
 * @brief  每次主循环最多发送一个字节，无数据或硬件忙时立即返回
 * @retval 无
 */
void bsp_uart_task(void)
{
    if ((s_tx_head != s_tx_tail) && (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TXE) != RESET))
    {
        huart3.Instance->TDR = s_tx_queue[s_tx_tail];
        s_tx_tail = (s_tx_tail + 1u) % UART_TX_QUEUE_SIZE;
    }
}

/**
 * @brief  非阻塞接收一个命令字节，同时恢复接收溢出状态
 * @param  out: 字节输出地址
 * @retval 1 收到字节；0 无数据或参数无效
 */
uint8_t bsp_uart_read(uint8_t *out)
{
    if (out == NULL)
    {
        return 0u;
    }
    // 模式切换等待时可能积累 ORE，先清标志，避免后续命令持续受影响。
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    return (HAL_UART_Receive(&huart3, out, 1u, 0u) == HAL_OK) ? 1u : 0u;
}

/**
 * @brief  格式化日志并入队，由主循环后台发送
 * @param  fmt: printf 风格格式串
 * @note   仅主循环调用；单条最多 127 字节，队列满时整条丢弃。
 */
void bsp_uart_printf(const char *fmt, ...)
{
    va_list args;
    uint32_t available;

    va_start(args, fmt);
    // vsnprintf 保证截断，避免缓冲区越界
    int32_t len = vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, args);
    va_end(args);

    if (len <= 0)
    {
        return;
    }
    if ((uint32_t)len >= sizeof(s_log_buf))
    {
        len = (int32_t)sizeof(s_log_buf) - 1;
    }

    available = (s_tx_tail + UART_TX_QUEUE_SIZE - s_tx_head - 1u) % UART_TX_QUEUE_SIZE;
    if ((uint32_t)len > available)
    {
        return;
    }
    for (uint32_t i = 0u; i < (uint32_t)len; i++)
    {
        s_tx_queue[s_tx_head] = (uint8_t)s_log_buf[i];
        s_tx_head = (s_tx_head + 1u) % UART_TX_QUEUE_SIZE;
    }
}
