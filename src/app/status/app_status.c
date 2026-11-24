/** @file app_status.c
 *  @brief 通过现有串口 DMA 输出 50 Hz VOFA+ JustFloat 波形。
 */
#include "app_status.h"
#include "app_control.h"
#include "app_log.h"
#include "board.h"
#include "perf.h"

#include <math.h>

#define STREAM_PERIOD_MS 20u

static uint32_t s_last_frame_ms;
static uint8_t s_need_sync;

/* ---------------- task ---------------- */

/** @brief 每 20 ms 读取最新反馈；主循环被阻塞时跳过错过的帧。 */
void app_status_task(void)
{
    app_control_feedback_t feedback;
    uint32_t now, elapsed;

    if (app_log_is_streaming() == 0u) { return; }
    now = bsp_board_get_millis();
    elapsed = now - s_last_frame_ms;
    if (elapsed < STREAM_PERIOD_MS) { return; }

    app_perf_begin_task(APP_PERF_TASK_STATUS);
    s_last_frame_ms = now - (elapsed % STREAM_PERIOD_MS);
    app_control_get_feedback(&feedback);
    // 固定角度电流环没有速度给定或反馈；停机与校准时也没有新的电流样本。
    const uint8_t current_valid = (uint8_t)((feedback.running != 0u) && (feedback.samples != 0u));
    // IEEE 754 单精度正无穷在 G474 上编码为 00 00 80 7F，即 JustFloat 帧尾。
    // 首包额外发送一个帧尾，先结束串口中已有的启动文字或命令回显。
    const float packet[] = {
        INFINITY,
        current_valid ? feedback.foc.iq : NAN,
        current_valid ? feedback.foc.id : NAN,
        current_valid ? feedback.iq_ref : NAN,
        app_control_get_angle(),
        NAN, NAN,
        INFINITY
    };
    const uint32_t skip = (s_need_sync != 0u) ? 0u : (uint32_t)sizeof(float);
    if (app_log_write_frame((const uint8_t *)packet + skip, (uint32_t)sizeof(packet) - skip) != 0u)
    {
        s_need_sync = 0u;
    }
    app_perf_end_task();
}

/* ---------------- write ---------------- */

/** @brief 启停连续波形；开启后从新的 20 ms 周期开始。 */
void app_status_toggle_stream(void)
{
    if (app_log_is_streaming() != 0u)
    {
        app_log_set_streaming(0u);
        return;
    }
    s_last_frame_ms = bsp_board_get_millis();
    s_need_sync = 1u;
    app_log_set_streaming(1u);
}
