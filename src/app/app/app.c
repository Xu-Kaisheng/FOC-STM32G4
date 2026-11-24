/**
 * @file    app.c
 * @brief   v0.1 应用主循环：初始化、模式联锁、调度
 *
 * 本文件只做编排：电流环策略在 `app/control`，命令在 `app_cmd`，
 * 打印在 `app_status`，统计在 `app_perf`，日志队列在 `app_log`。
 *
 * 为什么不拆成多个文件（250 行软限例外）：初始化、模式联锁与主循环共用
 * 同一份模式状态与启动顺序，拆开只会增加文件数与 include。
 */
#include "app.h"
#include "app_cmd.h"
#include "app_control.h"
#include "app_log.h"
#include "app_state.h"
#include "app_status.h"
#include "adc_cur.h"
#include "adc_vbus.h"
#include "board.h"
#include "hall.h"
#include "perf.h"
#include "pwm.h"
#include "uart.h"

/* 自检持续时间与测试占空比 */
#define BOOT_TEST_MS     3000u
#define BOOT_TEST_DUTY   0.50f

/* 模式切换时保持关桥，满足驱动板过流锁存的复位等待时间。 */
#define BRIDGE_RESET_WAIT_MS 200u

/* 上电诊断留给查询命令读取，独立于运行时的保护锁存。 */
static uint8_t s_boot_current_adc_ready;
static const char *s_boot_vbus;
static const char *s_boot_pwm;

/* ---------------- init ---------------- */

/**
 * @brief  应用初始化
 * @note   顺序：先关桥 -> 初始化 PWM -> 校准 ADC -> 启动 PWM（桥仍关断）。
 *         任何一步失败都锁存 ADC 不可用，保证 app_set_mode 无法开桥。
 */
void app_init(void)
{
    adc_vbus_sample_t vbus = {0};
    s_boot_vbus = "OK";
    s_boot_pwm = "skipped";

    bsp_uart_init();
    app_log_init();
    app_cmd_init();
    app_control_init();

    bsp_pwm_init();              /* 内部会先关 CTRL_SD */
    s_boot_current_adc_ready = bsp_adc_cur_init();
    app_set_adc_ready(s_boot_current_adc_ready);
    if (bsp_adc_vbus_init() == 0u)
    {
        app_set_adc_ready(0u);
        s_boot_vbus = "init failed";
    }
    else if (bsp_adc_vbus_read(&vbus) == 0u)
    {
        // 启动前先验证规则组链路，避免首次状态打印之前带着采样故障开桥。
        app_set_adc_ready(0u);
        s_boot_vbus = "read failed";
    }
    bsp_hall_init();

    bsp_pwm_stop();              /* 计数器停，输出空闲电平 */
    (void)bsp_pwm_enable_bridge(0u);

    if (bsp_board_get_boot_test() != 0u)
    {
        // 自检期间 H 桥始终关断（CTRL_SD=0），功率级不出力，只验证频率与死区。
        // 但即使桥是关的，也必须先确认"六个输出都真的打开了"——
        // 否则示波器上看到的上管波形会让人误判整个功率级正常。
        bsp_pwm_set_duty(BOOT_TEST_DUTY, BOOT_TEST_DUTY, BOOT_TEST_DUTY);
        bsp_pwm_start();
        if (bsp_pwm_check_ready() == 0u)
        {
            bsp_pwm_stop();
            s_boot_pwm = "failed";
        }
        else
        {
            s_boot_pwm = "OK";
            bsp_board_delay_ms(BOOT_TEST_MS);
            bsp_pwm_stop();
            bsp_pwm_set_duty_raw(0.0f, 0.0f, 0.0f);
        }
    }

    // 启动校准与自检不混入运行负载窗口。
    app_perf_init();
}

/* ---------------- task ---------------- */

/**
 * @brief  应用主循环任务
 */
void app_task(void)
{
    uint8_t command;

    // 1) 日志发送：模块内部只计队列回收和发送推进，空轮询不计入工作。
    app_log_task();

    // 2) 串口命令：非阻塞读，读不到立即返回
    command = app_cmd_task();

    // 3) 控制策略：维护母线输入与故障收尾，电流环由采样 ISR 推进
    app_control_task();

    // 4) 霍尔监视模式：只在状态变化时打印
    if ((command == 'h') && (app_get_mode() == APP_MODE_HALL_MON))
    {
        hall_state_t h = bsp_hall_read();
        if (h != app_get_last_hall())
        {
            app_perf_begin_task(APP_PERF_TASK_HALL);
            app_set_last_hall(h);
            app_log_write("HALL -> %s\r\n", bsp_hall_get_state_str(h));
            app_perf_end_task();
        }
    }

    app_perf_task();
    app_status_task();
}

/* ---------------- write ---------------- */

/** @brief 应答查询命令，显示启动诊断、当前霍尔与锁存故障。 */
void app_print_diagnostics(void)
{
    app_log_write("[BOARD] %s  HALL=%s\r\n", bsp_board_get_name(), bsp_hall_get_state_str(bsp_hall_read()));
    app_log_write("[BOOT] current ADC=%s  VBUS ADC=%s  PWM test=%s\r\n",
                  (s_boot_current_adc_ready != 0u) ? "OK" : "failed", s_boot_vbus, s_boot_pwm);
    app_log_write("[FAULT] code=%u\r\n", (unsigned)app_get_fault());
}

/**
 * @brief  进入指定模式
 * @param  mode: 目标模式
 */
void app_set_mode(app_mode_t mode)
{
    if ((mode == app_get_mode()) && (mode != APP_MODE_STOP))
    {
        return;
    }

    // 任何模式切换都先断电，再按新模式配置，避免带载切换
    app_control_stop();
    // 只在主循环中等待；此时桥和 PWM 都已关闭，保证重新使能前低电平足够长。
    app_status_wait_with_stream(BRIDGE_RESET_WAIT_MS);

    app_set_mode_raw(mode);

    switch (mode)
    {
        case APP_MODE_STOP:
            app_log_write("[MODE] STOP\r\n");
            break;

        case APP_MODE_FOC:
            if (bsp_board_get_drive_allowed() == 0u)
            {
                // 照片不能证明板载占用与功率级接线正确，未核验平台只能监视。
                app_set_mode_raw(APP_MODE_STOP);
                app_log_write("[LOCK] Board wiring not verified, monitor only\r\n");
                break;
            }
            if (app_get_adc_ready() == 0u)
            {
                app_set_mode_raw(APP_MODE_STOP);
                app_log_write("[FATAL] ADC unavailable, reset required\r\n");
                break;
            }
            if (app_control_start() == 0u)
            {
                app_set_mode_raw(APP_MODE_STOP);
                app_log_write("[FATAL] Current-loop interlock rejected, stay in STOP\r\n");
                break;
            }
            app_log_write("[MODE] FOC current, angle=%.0f deg, Id=%.2f Iq=%.2f A\r\n",
                          (double)app_control_get_angle(), (double)app_control_get_id(),
                          (double)app_control_get_iq());
            break;

        case APP_MODE_HALL_MON:
        {
            hall_state_t hall = bsp_hall_read();
            app_set_last_hall(hall);
            app_log_write("[MODE] HALL MON, now %s\r\n", bsp_hall_get_state_str(hall));
            break;
        }

        default:
            break;
    }
}
