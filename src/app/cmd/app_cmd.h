/**
 * @file    app_cmd.h
 * @brief   串口命令接收与解析
 */
#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>

/* ---------------- init ---------------- */

/** @brief 清除当前命令。 */
void app_cmd_init(void);

/* ---------------- task ---------------- */

/**
 * @brief  主循环命令任务：非阻塞读一个字节，有数据就解析
 * @retval 当前有效命令；0 表示无持续输出。
 */
uint8_t app_cmd_task(void);

#endif /* APP_CMD_H */
