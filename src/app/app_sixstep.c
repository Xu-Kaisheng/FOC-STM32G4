/**
 * @file    app_sixstep.c
 * @brief   v0.1 开环六步换相 + 霍尔/电流监视
 */
#include "app_sixstep.h"

#include "../bsp/bsp_uart.h"
#include "../bsp/bsp_perf.h"
#include "../drv/tim_pwm.h"
#include "../drv/adc_cur.h"
#include "../drv/adc_vbus.h"
#include "../drv/hall.h"
#include "app_board.h"

/* 运行模式 */
typedef enum
{
    MODE_STOP = 0,      /* 停机：H 桥关断 */
    MODE_SIXSTEP_FWD,   /* 开环六步，正转 */
    MODE_SIXSTEP_REV,   /* 开环六步，反转 */
    MODE_HALL_MON       /* 霍尔监视：只读不驱动 */
} app_mode_t;

/* 六步换相表：每步给 (导通高相, 导通低相)，0=U 1=V 2=W
 * 顺序 U+V- -> U+W- -> V+W- -> V+U- -> W+U- -> W+V- */
static const uint8_t s_step_table[6][2] =
{
    { PHASE_U, PHASE_V },
    { PHASE_U, PHASE_W },
    { PHASE_V, PHASE_W },
    { PHASE_V, PHASE_U },
    { PHASE_W, PHASE_U },
    { PHASE_W, PHASE_V }
};

static app_mode_t s_mode       = MODE_STOP;
static uint8_t    s_step       = 0u;      /* 当前换相步 0~5 */
static float      s_duty       = 0.20f;   /* 当前占空比（电压幅值） */
static float      s_elec_hz    = 1.0f;    /* 电频率，机械转速 = f/极对数 */
static uint32_t   s_last_step_ms = 0u;
static uint32_t   s_last_print_ms = 0u;
static hall_state_t s_last_hall = HALL_INVALID;
/* ADC 初始化或采样失败后锁住启动入口，避免使用无效电流继续运行。 */
static uint8_t s_adc_ready = 0u;

/* 自检持续时间，单位 ms */
#define BOOT_TEST_MS        3000u
#define BOOT_TEST_DUTY      0.50f

#define DUTY_STEP        0.05f
#define DUTY_LIMIT_MAX   0.60f   /* 台架调试先留余量，别一上来就满压 */
#define DUTY_LIMIT_MIN   0.05f
#define ELEC_HZ_MIN      0.5f
#define ELEC_HZ_MAX      20.0f
#define PRINT_PERIOD_MS  500u
/* 模式切换时保持关桥，满足驱动板过流锁存的复位等待时间。 */
#define BRIDGE_RESET_WAIT_MS 200u

/**
 * @brief  按当前步驱动三相
 * @note   先关掉不参与的相（浮空），再设占空比，最后打开两个导通相，
 *         这样顺序保证 MOE 由后两步的写 CCER 保持有效
 */
static void apply_step(void)
{
    uint32_t perf_start = bsp_perf_control_begin();
    uint8_t hi = s_step_table[s_step][0];
    uint8_t lo = s_step_table[s_step][1];
    float   d[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t i;

    d[hi] = s_duty;   /* 高相斩波 */
    d[lo] = 0.0f;     /* 低相上管常关，下管常通 */

    // 1) 只关掉不参与导通的相（保持另一相连续导通，避免换相时电流被打断）
    for (i = 0u; i < 3u; i++)
    {
        if ((i != (uint32_t)hi) && (i != (uint32_t)lo))
        {
            pwm_phase_enable((pwm_phase_t)i, 0u);
        }
    }

    // 2) 写占空比
    pwm_set_duty_raw(d[PHASE_U], d[PHASE_V], d[PHASE_W]);

    // 3) 打开参与导通的两相
    pwm_phase_enable((pwm_phase_t)hi, 1u);
    pwm_phase_enable((pwm_phase_t)lo, 1u);
    bsp_perf_control_end(BSP_PERF_SIXSTEP, perf_start);
}

/**
 * @brief  进入指定模式
 * @param  mode: 目标模式
 */
static void app_set_mode(app_mode_t mode)
{
    if (mode == s_mode)
    {
        return;
    }

    // 任何模式切换都先断电，再按新模式配置，避免带载切换
    pwm_bridge_enable(0u);
    pwm_stop();
    // 只在主循环中等待；此时桥和 PWM 都已关闭，保证重新使能前低电平足够长。
    app_board_delay(BRIDGE_RESET_WAIT_MS);

    s_mode = mode;

    switch (mode)
    {
        case MODE_STOP:
            bsp_uart_printf("[MODE] STOP\r\n");
            break;

        case MODE_SIXSTEP_FWD:
        case MODE_SIXSTEP_REV:
            if (app_board_drive_allowed() == 0u)
            {
                // 照片不能证明板载占用与功率级接线正确，未核验平台只能监视。
                s_mode = MODE_STOP;
                bsp_uart_printf("[LOCK] Board wiring not verified, monitor only\r\n");
                break;
            }
            if (s_adc_ready == 0u)
            {
                s_mode = MODE_STOP;
                bsp_uart_printf("[FATAL] ADC unavailable, reset required\r\n");
                break;
            }
            s_step = 0u;
            s_last_step_ms = app_board_millis();
            // 先把占空比清零并启动计数器/输出（HAL_TIM_PWM_Start 会把三路都打开），
            // 再由 apply_step 把不参与的相单独关掉——顺序不能反，
            // 否则 pwm_start 会把刚悬空的相又打开。
            pwm_set_duty_raw(0.0f, 0.0f, 0.0f);
            pwm_start();

            // 安全联锁：六路输出/死区没验证通过，绝不允许把 CTLR_SD 拉高，
            // 否则会出现"桥已使能但只有上管在斩波"的危险工况
            if (pwm_check_ready() == 0u)
            {
                pwm_stop();
                s_mode = MODE_STOP;
                bsp_uart_printf("[FATAL] PWM not ready, stay in STOP\r\n");
                break;
            }

            apply_step();
            if (pwm_bridge_enable_sixstep((pwm_phase_t)s_step_table[s_step][0],
                                          (pwm_phase_t)s_step_table[s_step][1]) == 0u)
            {
                pwm_stop();
                s_mode = MODE_STOP;
                bsp_uart_printf("[FATAL] Six-step interlock rejected, stay in STOP\r\n");
                break;
            }
            bsp_uart_printf("[MODE] SIXSTEP %s, f=%.1f Hz, duty=%.0f%%\r\n",
                            (mode == MODE_SIXSTEP_FWD) ? "FWD" : "REV",
                            (double)s_elec_hz, (double)(s_duty * 100.0f));
            break;

        case MODE_HALL_MON:
            s_last_hall = hall_read();
            bsp_uart_printf("[MODE] HALL MON, now %s\r\n", hall_state_str(s_last_hall));
            break;

        default:
            break;
    }
}

/**
 * @brief  打印一行状态
 */
static void print_status(void)
{
    adc_cur_sample_t cur = {0};
    adc_vbus_sample_t vbus = {0};
    if (s_adc_ready == 0u)
    {
        bsp_uart_printf("H=%s | ADC=INVALID\r\n", hall_state_str(hall_read()));
        return;
    }
    if (adc_vbus_read(&vbus) == 0u)
    {
        // 母线监视链路失效也要先关桥，禁止继续打印旧电压或重新启动。
        s_adc_ready = 0u;
        app_set_mode(MODE_STOP);
        bsp_uart_printf("[FATAL] VBUS ADC read failed, reset required\r\n");
        return;
    }
    if (adc_cur_read(&cur) == 0u)
    {
        // 采样链路失效必须先关桥；锁存到复位，禁止把零值误报为正常电流。
        s_adc_ready = 0u;
        app_set_mode(MODE_STOP);
        bsp_uart_printf("[FATAL] ADC read failed, reset required\r\n");
        return;
    }

    bsp_uart_printf("H=%s f=%.1fHz D=%2.0f%% | Iu=%+5.2f Iv=%+5.2f Iw=%+5.2f A | raw=%4u,%4u,%4u\r\n",
                    hall_state_str(hall_read()),
                    (double)s_elec_hz,
                    (double)(s_duty * 100.0f),
                    (double)cur.iu, (double)cur.iv, (double)cur.iw,
                    cur.raw[0], cur.raw[1], cur.raw[2]);
    // 独立一行，避免超过串口单条日志的 127 字节上限。
    bsp_uart_printf("VBUS=%.2f V | raw=%4u\r\n", (double)vbus.voltage, vbus.raw);
}

/**
 * @brief  处理串口命令
 * @param  c: 收到的字符
 */
static void handle_cmd(uint8_t c)
{
    switch (c)
    {
        case '0':
            app_set_mode(MODE_STOP);
            break;

        case '1':
            app_set_mode(MODE_SIXSTEP_FWD);
            break;

        case '2':
            app_set_mode(MODE_SIXSTEP_REV);
            break;

        case 'h':
            app_set_mode(MODE_HALL_MON);
            break;

        case '+':
            s_duty += DUTY_STEP;
            if (s_duty > DUTY_LIMIT_MAX) { s_duty = DUTY_LIMIT_MAX; }
            bsp_uart_printf("[DUTY] %.0f%%\r\n", (double)(s_duty * 100.0f));
            break;

        case '-':
            s_duty -= DUTY_STEP;
            if (s_duty < DUTY_LIMIT_MIN) { s_duty = DUTY_LIMIT_MIN; }
            bsp_uart_printf("[DUTY] %.0f%%\r\n", (double)(s_duty * 100.0f));
            break;

        case '>':
            s_elec_hz *= 1.5f;
            if (s_elec_hz > ELEC_HZ_MAX) { s_elec_hz = ELEC_HZ_MAX; }
            bsp_uart_printf("[FREQ] %.2f Hz\r\n", (double)s_elec_hz);
            break;

        case '<':
            s_elec_hz /= 1.5f;
            if (s_elec_hz < ELEC_HZ_MIN) { s_elec_hz = ELEC_HZ_MIN; }
            bsp_uart_printf("[FREQ] %.2f Hz\r\n", (double)s_elec_hz);
            break;

        case '?':
            bsp_uart_printf("\r\n-- STM32-FOC v0.1 --\r\n"
                            " 0 : stop\r\n"
                            " 1 : six-step forward\r\n"
                            " 2 : six-step reverse\r\n"
                            " h : hall monitor\r\n");
            // 分成两条日志，避免帮助正文超过单条格式化缓冲区而被截断。
            bsp_uart_printf(" +/- : duty +-5%%\r\n"
                            " >/< : electrical freq x/÷1.5\r\n"
                            " ? : this help\r\n");
            break;

        default:
            // 忽略未知字符（含回车换行）
            break;
    }
}

/**
 * @brief  应用初始化
 * @note   顺序：先关桥 -> 初始化 PWM -> 校准 ADC -> 启动 PWM（桥仍关断）
 */
void app_init(void)
{
    adc_vbus_sample_t vbus = {0};
    bsp_uart_init();

    pwm_init();              /* 内部会先关 CTRL_SD */
    s_adc_ready = adc_cur_init();
    if (s_adc_ready == 0u)
    {
        bsp_uart_printf("[FATAL] ADC init failed, start disabled\r\n");
    }
    if (adc_vbus_init() == 0u)
    {
        s_adc_ready = 0u;
        bsp_uart_printf("[FATAL] VBUS ADC init failed, start disabled\r\n");
    }
    else if (adc_vbus_read(&vbus) == 0u)
    {
        // 启动前先验证规则组链路，避免首次状态打印之前带着采样故障开桥。
        s_adc_ready = 0u;
        bsp_uart_printf("[FATAL] VBUS ADC read failed, start disabled\r\n");
    }
    hall_init();

    pwm_stop();              /* 计数器停，输出空闲电平 */
    pwm_bridge_enable(0u);

    if (app_board_boot_test() != 0u)
    {
        // 自检期间 H 桥始终关断（CTRL_SD=0），功率级不出力，只验证频率与死区。
        // 但即使桥是关的，也必须先确认"六个输出都真的打开了"——
        // 否则示波器上看到的上管波形会让人误判整个功率级正常。
        // 启动前写入测试占空比，由 pwm_start 提交预装载，首周期即使用测试值。
        pwm_set_duty(BOOT_TEST_DUTY, BOOT_TEST_DUTY, BOOT_TEST_DUTY);
        pwm_start();
        if (pwm_check_ready() == 0u)
        {
            pwm_stop();
            bsp_uart_printf("[FATAL] PWM self-check failed (CCER/MOE/DTG), test skipped\r\n");
        }
        else
        {
            bsp_uart_printf("[TEST] PWM 20kHz 50%% for %u ms, bridge OFF\r\n",
                            (unsigned)BOOT_TEST_MS);
            // 自检等待期间继续输出日志，让示波器操作提示及时可见；功率级仍关断。
            uint32_t test_start_ms = app_board_millis();
            while ((app_board_millis() - test_start_ms) < BOOT_TEST_MS)
            {
                bsp_uart_task();
            }
            pwm_stop();
            pwm_set_duty_raw(0.0f, 0.0f, 0.0f);
            bsp_uart_printf("[TEST] done, PWM OFF, bridge OFF\r\n");
        }
    }

    s_last_hall = hall_read();

    bsp_uart_printf("[BOARD] %s\r\n", app_board_name());
    bsp_uart_printf("\r\nSTM32-FOC v0.1 boot\r\n"
                    "HALL=%s  (send '?' for help)\r\n",
                    hall_state_str(s_last_hall));
    // 启动校准与自检不混入运行负载窗口。
    bsp_perf_init();
}

/**
 * @brief  主循环任务
 */
void app_task(void)
{
    uint8_t  ch = 0u;
    uint32_t now;

    // 1) 串口命令（非阻塞读，读不到就走）
    bsp_uart_task();
    if (bsp_uart_read(&ch) != 0u)
    {
        bsp_perf_work_begin();
        handle_cmd(ch);
        bsp_perf_work_end();
    }

    // 模式切换可能等待 200 ms 并重置换相时间，必须在命令处理后重新取时。
    now = app_board_millis();

    // 2) 六步换相定时
    if ((s_mode == MODE_SIXSTEP_FWD) || (s_mode == MODE_SIXSTEP_REV))
    {
        // 每电周期 6 步，步间隔 = 1000 / (6 * f_e)
        uint32_t step_ms = (uint32_t)(1000.0f / (6.0f * s_elec_hz));
        if (step_ms == 0u)
        {
            step_ms = 1u;
        }

        if ((now - s_last_step_ms) >= step_ms)
        {
            s_last_step_ms = now;
            if (s_mode == MODE_SIXSTEP_FWD)
            {
                s_step = (uint8_t)((s_step + 1u) % 6u);
            }
            else
            {
                s_step = (uint8_t)((s_step + 5u) % 6u);
            }
            apply_step();
        }
    }

    // 3) 霍尔监视模式：只在状态变化时打印
    if (s_mode == MODE_HALL_MON)
    {
        hall_state_t h = hall_read();
        if (h != s_last_hall)
        {
            bsp_perf_work_begin();
            s_last_hall = h;
            bsp_uart_printf("HALL -> %s\r\n", hall_state_str(h));
            bsp_perf_work_end();
        }
    }

    // 4) 周期性状态打印
    if ((now - s_last_print_ms) >= PRINT_PERIOD_MS)
    {
        s_last_print_ms = now;
        if ((s_mode != MODE_STOP) || (s_last_hall == HALL_INVALID))
        {
            s_last_hall = hall_read();
        }
        bsp_perf_work_begin();
        print_status();
        bsp_perf_work_end();
    }
    bsp_perf_task();
}
