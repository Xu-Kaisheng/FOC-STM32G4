/**
 * @file    app_log.c
 * @brief   调试日志队列与主循环推进
 *
 * 生产者（app_log_write）与消费者（app_log_task）都在主循环；
 * DMA 完成由 BSP 的忙标志通知，本模块轮询该标志后推进队列尾部。
 * 单人操作队列索引，因此不需要临界区。
 */
#include "app_log.h"
#include "perf.h"
#include "uart.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static char s_log_buf[APP_LOG_LINE_MAX];
static uint8_t s_queue[APP_LOG_QUEUE_SIZE];
static uint32_t s_head = 0u;
static uint32_t s_tail = 0u;
/* 已交给 DMA、尚未完成的字节数；完成前这些字节仍占用队列空间。 */
static uint32_t s_pending = 0u;
/* 0 表示没有文字；非零只保留判定浮点对齐所需的长度信息。 */
static uint32_t s_reply_bytes;
static uint8_t s_reply_needs_lf, s_reply_tail_pending;

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

/** @brief 整段入队；空间不足时保留原队列，避免截断二进制帧。 */
static uint8_t enqueue_bytes(const uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0u) || (len > log_free())) { return 0u; }
    for (uint32_t i = 0u; i < len; i++)
    {
        s_queue[s_head] = data[i];
        s_head = (s_head + 1u) % APP_LOG_QUEUE_SIZE;
    }
    return 1u;
}

/* ---------------- init ---------------- */

/**
 * @brief  复位队列与发送状态
 */
void app_log_init(void)
{
    s_reply_bytes = 0u;
    s_reply_needs_lf = 0u;
    s_reply_tail_pending = 0u;
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

    // 等待 DMA 或空队列只是轮询；确认有队列工作后再开始计时。
    if ((bsp_uart_is_tx_busy() != 0u) ||
        ((s_pending == 0u) && (log_span() == 0u)))
    {
        return;
    }
    app_perf_begin_task(APP_PERF_TASK_LOG);

    // 上一段 DMA 已完成：推进尾部，释放队列空间。
    if (s_pending != 0u)
    {
        s_tail = (s_tail + s_pending) % APP_LOG_QUEUE_SIZE;
        s_pending = 0u;
    }

    if (s_reply_tail_pending != 0u) { (void)app_log_finish_vofa_reply(); }
    len = log_span();
    if ((len != 0u) && (bsp_uart_write(&s_queue[s_tail], len) != 0u))
    {
        s_pending = len;
    }
    app_perf_end_task();
}

/* ---------------- write ---------------- */

/** @brief 为本轮文字应答追加 JustFloat 帧尾，让上位机将其识别为独立的非采样帧。 */
uint8_t app_log_finish_vofa_reply(void)
{
    static const uint8_t suffix[] = {'\n', '\n', 0x00u, 0x00u, 0x80u, 0x7fu};
    uint32_t newlines = s_reply_needs_lf;
    if (s_reply_bytes == 0u) { return 1u; }
    // 有效采样载荷必须是 4 字节的倍数，文字已对齐时再补一个换行。
    if (((s_reply_bytes + newlines) % sizeof(float)) == 0u) { newlines++; }
    if (enqueue_bytes(&suffix[2u - newlines], (uint32_t)sizeof(float) + newlines) == 0u)
    {
        s_reply_tail_pending = 1u;
        return 0u;
    }
    s_reply_tail_pending = 0u;
    s_reply_bytes = 0u;
    return 1u;
}

/** @brief 原子排入整帧，允许排在完整文字应答或在途 DMA 之后。 */
uint8_t app_log_write_frame(const uint8_t *data, uint32_t len)
{
    return enqueue_bytes(data, len);
}

/**
 * @brief  格式化日志并入队
 * @param  fmt: printf 风格格式串
 */
void app_log_write(const char *fmt, ...)
{
    va_list args;
    int32_t len;

    // 队列满时优先保证前一条应答的边界。
    if (s_reply_tail_pending != 0u) { return; }
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
    if (enqueue_bytes((const uint8_t *)s_log_buf, (uint32_t)len) != 0u)
    {
        s_reply_bytes = (s_reply_bytes % (uint32_t)sizeof(float)) + (uint32_t)len;
        s_reply_needs_lf = (uint8_t)(s_log_buf[len - 1] != '\n');
    }
}
