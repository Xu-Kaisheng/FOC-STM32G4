/**
 * @file    app_cmd.c
 * @brief   串口单字符命令解析
 */
#include "app_cmd.h"
#include "app.h"
#include "app_control.h"
#include "app_log.h"
#include "app_status.h"
#include "perf.h"
#include "uart.h"

/* 命令每次调整的步长与显示比例；安全范围由 app_control_set_* 负责钳位。 */
#define APP_CURRENT_STEP_A 0.1f
#define APP_ANGLE_STEP_DEG 15.0f

static uint8_t s_active_command;

/* ---------------- internal ---------------- */

/**
 * @brief  解析一个命令字符
 * @param  c: 收到的字符，未知字符（含回车换行）忽略
 */
static void handle_command(uint8_t c)
{
    switch (c)
    {
        case '0':
            app_set_mode(APP_MODE_STOP);
            break;

        case '1':
            app_set_mode(APP_MODE_FOC);
            break;

        case 'h':
            app_set_mode(APP_MODE_HALL_MON);
            break;

        case '+':
        case '-':
        {
            float delta = (c == '+') ? APP_CURRENT_STEP_A : -APP_CURRENT_STEP_A;
            float current = app_control_set_iq(app_control_get_iq() + delta);
            app_log_write("[Iq] %.2f A\r\n", (double)current);
            break;
        }

        case ']':
        case '[':
        {
            float delta = (c == ']') ? APP_CURRENT_STEP_A : -APP_CURRENT_STEP_A;
            float current = app_control_set_id(app_control_get_id() + delta);
            app_log_write("[Id] %.2f A\r\n", (double)current);
            break;
        }

        case '>':
        case '<':
        {
            float delta = (c == '>') ? APP_ANGLE_STEP_DEG : -APP_ANGLE_STEP_DEG;
            float angle = app_control_set_angle(app_control_get_angle() + delta);
            app_log_write("[ANGLE] %.0f deg (editable in STOP)\r\n", (double)angle);
            break;
        }

        case 'p':
            app_perf_request_report();
            break;

        case 'v':
            app_status_set_stream(1u);
            return;

        case 'c':
            app_status_set_stream(0u);
            return;

        case '?':
            app_print_diagnostics();
            app_log_write("\r\n-- STM32-FOC v0.1 --\r\n"
                            " 0 : stop\r\n"
                            " 1 : fixed-angle current loop\r\n"
                            " h : hall monitor\r\n");
            // 分条打印，避免帮助正文超过单条格式化缓冲区而被截断。
            app_log_write(" +/- : Iq +-0.1 A\r\n"
                            " ]/[ : Id +-0.1 A\r\n"
                            " >/< : angle +-15 deg in STOP\r\n"
                            " p : task/IRQ load (once)\r\n");
            app_log_write(" v : start VOFA+ 50 Hz; c : stop stream\r\n"
                            " ? : diagnostics and help\r\n");
            break;

        default:
            // 串口工具附加的回车换行或无效字节不会打断当前命令。
            return;
    }
    s_active_command = c;
}

/* ---------------- init ---------------- */

/** @brief 上电后等待第一条有效命令。 */
void app_cmd_init(void)
{
    s_active_command = 0u;
    app_status_set_stream(0u);
}

/* ---------------- task ---------------- */

/**
 * @brief  主循环命令任务
 */
uint8_t app_cmd_task(void)
{
    uint8_t c = 0u;

    if (bsp_uart_read_byte(&c) != 0u)
    {
        app_perf_begin_task(APP_PERF_TASK_CMD);
        handle_command(c);
        app_perf_end_task();
    }
    return s_active_command;
}
