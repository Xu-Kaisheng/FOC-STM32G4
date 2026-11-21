/**
 * @file    uart.c
 * @brief   调试串口硬件实现：只负责 USART3 与 TX DMA 本身
 *
 * 队列、格式化和主循环推进在 `app/log`；本模块不包含任何 `_task`。
 */
#include "uart.h"
#include "usart.h"

#include <stddef.h>

/* DMA 发送忙标志：启动时置位，USART3 TC 或发送错误回调清零。 */
static volatile uint8_t s_tx_busy = 0u;

/* ---------------- init ---------------- */

/**
 * @brief  初始化硬件发送状态
 */
void bsp_uart_init(void)
{
    // 清掉可能残留的错误标志，否则首次发送会直接返回 HAL_ERROR
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    s_tx_busy = 0u;
}

/* ---------------- read ---------------- */

/**
 * @brief  非阻塞读取一个字节
 * @param  out: 字节输出地址
 * @retval 1 收到字节；0 无数据或参数无效
 */
uint8_t bsp_uart_read_byte(uint8_t *out)
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
 * @brief  查询 DMA 是否仍在发送
 * @retval 1 忙；0 空闲
 */
uint8_t bsp_uart_is_tx_busy(void)
{
    return s_tx_busy;
}

/* ---------------- write ---------------- */

/**
 * @brief  启动一次 DMA 发送
 * @param  data: 数据地址
 * @param  len: 字节数
 * @retval 1 已启动；0 忙、参数无效或 HAL 拒绝
 */
uint8_t bsp_uart_write(const uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0u) || (len > 65535u) || (s_tx_busy != 0u))
    {
        return 0u;
    }

    s_tx_busy = 1u;
    if (HAL_UART_Transmit_DMA(&huart3, data, (uint16_t)len) != HAL_OK)
    {
        s_tx_busy = 0u;
        return 0u;
    }
    return 1u;
}

/* ---------------- hook ---------------- */

/**
 * @brief  HAL 发送完成回调（USART3 TC 中断上下文）
 * @param  huart: 触发回调的串口句柄
 * @note   只清忙标志；队列推进由应用层的 app_log_task 轮询完成。
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart3)
    {
        s_tx_busy = 0u;
    }
}

/**
 * @brief  DMA 发送失败后释放忙标志，允许后续日志继续发送
 * @param  huart: 触发错误回调的串口句柄
 * @note   本串口仅 TX 使用 DMA；失败段不重发，由日志队列按原长度释放。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    // 接收溢出等错误不代表 TX 已停止，不能提前释放仍由 DMA 使用的缓冲区。
    if ((huart == &huart3) && ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0u))
    {
        s_tx_busy = 0u;
    }
}
