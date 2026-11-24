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
#define STREAM_TX_POLL_MS 1u

static uint32_t s_last_frame_ms;
static uint8_t s_stream_enabled;

/* ---------------- task ---------------- */

/** @brief 每 20 ms 读取最新反馈；主循环被阻塞时跳过错过的帧。 */
void app_status_task(void)
{
    app_control_feedback_t feedback;
    uint32_t now, elapsed;

    if (s_stream_enabled == 0u) { return; }
    // 命令和性能任务已输出本轮全部文字，先结束应答，再排入采样帧。
    if (app_log_finish_vofa_reply() == 0u) { return; }
    now = bsp_board_get_millis();
    elapsed = now - s_last_frame_ms;
    if (elapsed < STREAM_PERIOD_MS) { return; }

    app_perf_begin_task(APP_PERF_TASK_STATUS);
    s_last_frame_ms = now - (elapsed % STREAM_PERIOD_MS);
    app_control_get_feedback(&feedback);
    // 固定角度电流环没有速度给定或反馈；停机与校准时也没有新的电流样本。
    const uint8_t current_valid = (uint8_t)((feedback.running != 0u) && (feedback.samples != 0u));
    // IEEE 754 单精度正无穷在 G474 上编码为 00 00 80 7F，即 JustFloat 帧尾。
    const float packet[] = {
        current_valid ? feedback.foc.iq : NAN,
        current_valid ? feedback.foc.id : NAN,
        current_valid ? feedback.iq_ref : NAN,
        app_control_get_angle(),
        NAN, NAN,
        INFINITY
    };
    (void)app_log_write_frame((const uint8_t *)packet, sizeof(packet));
    app_perf_end_task();
}

/* ---------------- write ---------------- */

/** @brief 仅在从关闭转为开启时设置首帧时间；重复启动保持原节拍。 */
void app_status_set_stream(uint8_t enable)
{
    if ((enable != 0u) && (s_stream_enabled == 0u))
    {
        s_last_frame_ms = bsp_board_get_millis();
    }
    s_stream_enabled = enable;
}

/** @brief 模式切换保持关桥的整个等待期内，继续按原节拍采样和推进串口。 */
void app_status_wait_with_stream(uint32_t duration_ms)
{
    if (s_stream_enabled == 0u)
    {
        bsp_board_delay_ms(duration_ms);
        return;
    }
    uint32_t start = bsp_board_get_millis();
    while ((bsp_board_get_millis() - start) < duration_ms)
    {
        bsp_board_delay_ms(STREAM_TX_POLL_MS);
        // CMD、STATUS 和 LOG 分别统计独占时间，串口工作结束后恢复调用方计时。
        app_perf_task_t caller = app_perf_end_task();
        app_status_task();
        app_log_task();
        if (caller < APP_PERF_TASK_COUNT) { app_perf_begin_task(caller); }
    }
}
