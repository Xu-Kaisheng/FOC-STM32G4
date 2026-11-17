/**
 * @file    app_cmd.c
 * @brief   串口单字符命令解析
 */
#include "app_cmd.h"
#include "app.h"
#include "app_control.h"
#include "app_log.h"
#include "uart.h"

/* 命令每次调整的步长与显示比例；安全范围由 app_control_set_* 负责钳位。 */
#define APP_DUTY_STEP      0.05f
#define APP_FREQ_SCALE     1.5f
#define APP_PERCENT_SCALE  100.0f

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
            app_set_mode(APP_MODE_SIXSTEP_FWD);
            break;

        case '2':
            app_set_mode(APP_MODE_SIXSTEP_REV);
            break;

        case 'h':
            app_set_mode(APP_MODE_HALL_MON);
            break;

        case '+':
        {
            float duty = app_control_set_duty(app_control_get_duty() + APP_DUTY_STEP);
            app_log_write("[DUTY] %.0f%%\r\n", (double)(duty * APP_PERCENT_SCALE));
            break;
        }

        case '-':
        {
            float duty = app_control_set_duty(app_control_get_duty() - APP_DUTY_STEP);
            app_log_write("[DUTY] %.0f%%\r\n", (double)(duty * APP_PERCENT_SCALE));
            break;
        }

        case '>':
        {
            float hz = app_control_set_elec_hz(app_control_get_elec_hz() * APP_FREQ_SCALE);
            app_log_write("[FREQ] %.2f Hz\r\n", (double)hz);
            break;
        }

        case '<':
        {
            float hz = app_control_set_elec_hz(app_control_get_elec_hz() / APP_FREQ_SCALE);
            app_log_write("[FREQ] %.2f Hz\r\n", (double)hz);
            break;
        }

        case '?':
            app_log_write("\r\n-- STM32-FOC v0.1 --\r\n"
                            " 0 : stop\r\n"
                            " 1 : six-step forward\r\n"
                            " 2 : six-step reverse\r\n"
                            " h : hall monitor\r\n");
            // 分成两条日志，避免帮助正文超过单条格式化缓冲区而被截断。
            app_log_write(" +/- : duty +-5%%\r\n"
                            " >/< : electrical freq x/÷1.5\r\n"
                            " ? : this help\r\n");
            break;

        default:
            // 忽略未知字符（含回车换行）
            break;
    }
}

/* ---------------- task ---------------- */

/**
 * @brief  主循环命令任务
 */
void app_cmd_task(void)
{
    uint8_t c = 0u;

    if (bsp_uart_read_byte(&c) != 0u)
    {
        handle_command(c);
    }
}
