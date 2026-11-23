/**
 * @file    app_log.h
 * @brief   调试日志：格式化、静态队列、每轮主循环推进
 *
 * 属于应用层（换产品才改），因此不放在 BSP。BSP 只提供"启动一次 DMA 发送"
 * 的硬件接口 `bsp_uart_write()`；本模块负责把日志排队并在主循环里推进。
 *
 * 设计目标：任何时刻都不阻塞主循环；队列满时整条丢弃，绝不等待硬件。
 */
#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>

/* ---------------- 公共宏 ---------------- */

/* 单条日志的最大长度（含结尾 NUL）。128 字节够打一行状态。 */
#define APP_LOG_LINE_MAX  128u
/* 容纳完整的 1 s 状态/性能列表及命令应答，避免列表尾部被队列丢弃。 */
#define APP_LOG_QUEUE_SIZE 1024u

/* ---------------- init ---------------- */

/**
 * @brief  复位日志队列与发送状态（不初始化硬件）
 */
void app_log_init(void);

/* ---------------- task ---------------- */

/**
 * @brief  主循环推进：上一段发完后从队列取下一段交给 DMA，随后立即返回
 * @note   只能由主循环调用；不等待硬件。
 */
void app_log_task(void);

/* ---------------- write ---------------- */

/**
 * @brief  格式化日志并入队，由 app_log_task 与 DMA 后台发送
 * @param  fmt: printf 风格格式串
 * @note   禁止在中断里调用；单条最多 APP_LOG_LINE_MAX-1 字节，
 *         队列剩余空间不足时丢弃整条日志。
 */
void app_log_write(const char *fmt, ...);

#endif /* APP_LOG_H */
