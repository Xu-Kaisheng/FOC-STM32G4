/**
 * @file    app_cmd.h
 * @brief   串口命令接收与解析
 */
#ifndef APP_CMD_H
#define APP_CMD_H

/* ---------------- task ---------------- */

/**
 * @brief  主循环命令任务：非阻塞读一个字节，有数据就解析
 * @note   读不到数据立即返回，不阻塞控制环。
 */
void app_cmd_task(void);

#endif /* APP_CMD_H */
