/**
 * @file    app_control.c
 * @brief   算法策略：六步参数、步进调度与 BSP 回调注入
 */
#include "app_control.h"
#include "alg_sixstep.h"
#include "app_state.h"
#include "board.h"
#include "perf.h"
#include "pwm.h"

/* 台架调试范围：先留余量，别一上来就满压。 */
#define APP_CONTROL_DUTY_MIN      0.05f
#define APP_CONTROL_DUTY_MAX      0.60f
#define APP_CONTROL_ELEC_HZ_MIN   0.5f
#define APP_CONTROL_ELEC_HZ_MAX   20.0f

/* 初始值：占空比 20%、电频率 1 Hz。 */
#define APP_CONTROL_DUTY_DEFAULT     0.20f
#define APP_CONTROL_ELEC_HZ_DEFAULT  1.0f

/* 每电周期 6 步，步间隔 = 1000 / (6 * f_e) */
#define APP_CONTROL_STEP_PER_CYCLE  6.0f
#define APP_CONTROL_MS_PER_SECOND   1000.0f

static uint8_t  s_step = 0u;
static float    s_duty = APP_CONTROL_DUTY_DEFAULT;
static float    s_elec_hz = APP_CONTROL_ELEC_HZ_DEFAULT;
static uint32_t s_last_step_ms = 0u;

/* ---------------- internal ---------------- */

/** @brief 把算法的"单相开关"回调接到 BSP。 */
static void control_enable_phase(uint8_t phase, uint8_t enable)
{
    bsp_pwm_enable_phase((pwm_phase_t)phase, enable);
}

/** @brief 把算法的"三相占空比"回调接到 BSP。 */
static void control_set_duty_raw(float du, float dv, float dw)
{
    bsp_pwm_set_duty_raw(du, dv, dw);
}

static const alg_sixstep_io_t s_algo_io =
{
    control_enable_phase,
    control_set_duty_raw
};

/* ---------------- init ---------------- */

/**
 * @brief  复位参数并注入 BSP 输出接口
 */
void app_control_init(void)
{
    alg_sixstep_init(&s_algo_io);
    s_step = 0u;
    s_duty = APP_CONTROL_DUTY_DEFAULT;
    s_elec_hz = APP_CONTROL_ELEC_HZ_DEFAULT;
    s_last_step_ms = 0u;
}

/* ---------------- task ---------------- */

/**
 * @brief  主循环控制任务
 */
void app_control_task(void)
{
    app_mode_t mode = app_get_mode();
    uint32_t now;
    uint32_t step_ms;
    uint32_t perf_start;

    if ((mode != APP_MODE_SIXSTEP_FWD) && (mode != APP_MODE_SIXSTEP_REV))
    {
        return;
    }

    now = bsp_board_get_millis();
    step_ms = (uint32_t)(APP_CONTROL_MS_PER_SECOND / (APP_CONTROL_STEP_PER_CYCLE * s_elec_hz));
    if (step_ms == 0u)
    {
        step_ms = 1u;
    }
    if ((now - s_last_step_ms) < step_ms)
    {
        return;
    }
    s_last_step_ms = now;

    perf_start = app_perf_begin_control();
    s_step = alg_sixstep_next_step(s_step, (mode == APP_MODE_SIXSTEP_FWD) ? 1u : 0u);
    alg_sixstep_apply(s_step, s_duty);
    app_perf_end_control(APP_PERF_SIXSTEP, perf_start);
}

/* ---------------- read ---------------- */

/** @brief 当前换相步。 */
uint8_t app_control_get_step(void)
{
    return s_step;
}

/** @brief 当前占空比。 */
float app_control_get_duty(void)
{
    return s_duty;
}

/** @brief 当前电频率。 */
float app_control_get_elec_hz(void)
{
    return s_elec_hz;
}

/**
 * @brief  查询当前步的高相与低相
 * @param  high: 高相输出地址
 * @param  low:  低相输出地址
 */
void app_control_get_phases(uint8_t *high, uint8_t *low)
{
    alg_sixstep_get_phases(s_step, high, low);
}

/* ---------------- write ---------------- */

/**
 * @brief  启动六步输出
 * @retval 1 成功；0 自检或联锁失败且已停 PWM
 */
uint8_t app_control_start(void)
{
    uint8_t hi;
    uint8_t lo;

    s_step = 0u;
    s_last_step_ms = bsp_board_get_millis();

    // 先把占空比清零并启动计数器/输出（bsp_pwm_start 会把三路都打开），
    // 再由 alg_sixstep_apply 把不参与的相单独关掉——顺序不能反。
    bsp_pwm_set_duty_raw(0.0f, 0.0f, 0.0f);
    bsp_pwm_start();

    // 安全联锁：六路输出/死区没验证通过，绝不允许把 CTRL_SD 拉高。
    if (bsp_pwm_check_ready() == 0u)
    {
        bsp_pwm_stop();
        return 0u;
    }

    alg_sixstep_apply(s_step, s_duty);
    app_control_get_phases(&hi, &lo);
    if (bsp_pwm_enable_bridge_sixstep((pwm_phase_t)hi, (pwm_phase_t)lo) == 0u)
    {
        bsp_pwm_stop();
        return 0u;
    }
    return 1u;
}

/**
 * @brief  设置占空比并钳位
 * @param  duty: 0.0~1.0
 * @retval 钳位后的实际值
 */
float app_control_set_duty(float duty)
{
    if (duty < APP_CONTROL_DUTY_MIN)
    {
        duty = APP_CONTROL_DUTY_MIN;
    }
    else if (duty > APP_CONTROL_DUTY_MAX)
    {
        duty = APP_CONTROL_DUTY_MAX;
    }
    else
    {
        /* 保持在范围内 */
    }
    s_duty = duty;
    return s_duty;
}

/**
 * @brief  设置电频率并钳位
 * @param  elec_hz: 目标电频率
 * @retval 钳位后的实际值
 */
float app_control_set_elec_hz(float elec_hz)
{
    if (elec_hz < APP_CONTROL_ELEC_HZ_MIN)
    {
        elec_hz = APP_CONTROL_ELEC_HZ_MIN;
    }
    else if (elec_hz > APP_CONTROL_ELEC_HZ_MAX)
    {
        elec_hz = APP_CONTROL_ELEC_HZ_MAX;
    }
    else
    {
        /* 保持在范围内 */
    }
    s_elec_hz = elec_hz;
    return s_elec_hz;
}
