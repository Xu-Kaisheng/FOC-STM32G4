/**
 * @file    app_log.c
 * @brief   调试日志队列与主循环推进
 *
 * 生产者（app_log_write）与消费者（app_log_task）都在主循环；
 * DMA 完成由 BSP 的忙标志通知，本模块轮询该标志后推进队列尾部。
 * 单人操作队列索引，因此不需要临界区。
 */
#include "app_log.h"
#include "uart.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define APP_LOG_QUEUE_SIZE 512u

static char s_log_buf[APP_LOG_LINE_MAX];
static uint8_t s_queue[APP_LOG_QUEUE_SIZE];
static uint32_t s_head = 0u;
static uint32_t s_tail = 0u;
/* 已交给 DMA、尚未完成的字节数；完成前这些字节仍占用队列空间。 */
static uint32_t s_pending = 0u;

/* ---------------- internal ---------------- */

/** @brief 队列剩余可写字节数（含在途字节）。 */
static uint32_t log_free(void)
{
    return (s_tail + APP_LOG_QUEUE_SIZE - s_head - 1u) % APP_LOG_QUEUE_SIZE;
}

/** @brief 从队首到缓冲区末尾的连续字节数；0 表示队列空。 */
static uint32_t log_span(void)
{
    if (s_head == s_tail)
    {
        return 0u;
    }
    return (s_head > s_tail) ? (s_head - s_tail) : (APP_LOG_QUEUE_SIZE - s_tail);
}

/* ---------------- init ---------------- */

/**
 * @brief  复位队列与发送状态
 */
void app_log_init(void)
{
    s_head = 0u;
    s_tail = 0u;
    s_pending = 0u;
}

/* ---------------- task ---------------- */

/**
 * @brief  主循环推进日志发送
 * @note   环形缓冲跨边界时先发到末尾，剩余部分下一轮再发。
 */
void app_log_task(void)
{
    uint32_t len;

    // 上一段 DMA 已完成：推进尾部，释放队列空间。
    if (s_pending != 0u)
    {
        if (bsp_uart_is_tx_busy() != 0u)
        {
            return;
        }
        s_tail = (s_tail + s_pending) % APP_LOG_QUEUE_SIZE;
        s_pending = 0u;
    }

    len = log_span();
    if ((len == 0u) || (bsp_uart_write(&s_queue[s_tail], len) == 0u))
    {
        return;
    }
    s_pending = len;
}

/* ---------------- write ---------------- */

/**
 * @brief  格式化日志并入队
 * @param  fmt: printf 风格格式串
 */
void app_log_write(const char *fmt, ...)
{
    va_list args;
    int32_t len;
    uint32_t i;

    va_start(args, fmt);
    // vsnprintf 保证截断，避免缓冲区越界
    len = vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, args);
    va_end(args);

    if (len <= 0)
    {
        return;
    }
    if ((uint32_t)len >= sizeof(s_log_buf))
    {
        len = (int32_t)sizeof(s_log_buf) - 1;
    }
    if ((uint32_t)len > log_free())
    {
        return;
    }
    for (i = 0u; i < (uint32_t)len; i++)
    {
        s_queue[s_head] = (uint8_t)s_log_buf[i];
        s_head = (s_head + 1u) % APP_LOG_QUEUE_SIZE;
    }
}
