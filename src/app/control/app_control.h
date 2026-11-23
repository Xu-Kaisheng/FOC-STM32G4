/** @file app_control.h
 *  @brief 固定电角度电流环的参数、启停、反馈与中断入口。
 */
#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include "adc_cur.h"
#include "adc_vbus.h"
#include "alg_foc.h"
#include "app_state.h"

typedef struct
{
    adc_cur_sample_t phase;
    alg_foc_output_t foc;
    float id_ref, iq_ref;
    uint32_t samples;
    uint8_t running;
} app_control_feedback_t;

/* ---------------- init ---------------- */
/** @brief 复位应用控制状态，上电保持关桥。 */
void app_control_init(void);

/* ---------------- task ---------------- */
/** @brief 更新母线并收尾故障；电流调节在 ADC ISR 内运行。 */
void app_control_task(void);

/* ---------------- read ---------------- */
/** @brief 获取完整电流反馈快照，不关闭中断。 */
void app_control_get_feedback(app_control_feedback_t *out);
/** @brief 返回最近一次母线采样，首次采样前为零。 */
void app_control_get_bus(adc_vbus_sample_t *out);
/** @brief 获取 d 轴给定，A。 */
float app_control_get_id(void);
/** @brief 获取 q 轴给定，A。 */
float app_control_get_iq(void);
/** @brief 获取固定电角度，度。 */
float app_control_get_angle(void);

/* ---------------- write ---------------- */
/** @brief 准备同步采样，关桥标定完成后由 ISR 开桥；返回 0 表示拒绝。 */
uint8_t app_control_start(void);
/** @brief 先撤销运行许可并关桥，再在主循环停止 ADC。 */
void app_control_stop(void);
/** @brief d 轴给定钳位到台架范围，运行时按斜坡跟随。 */
float app_control_set_id(float value);
/** @brief q 轴给定钳位到台架范围，运行时按斜坡跟随。 */
float app_control_set_iq(float value);
/** @brief 仅停机时设置电角度，返回实际角度。 */
float app_control_set_angle(float degrees);
/** @brief 故障立即关桥并锁存，ISR 中无日志或等待。 */
void app_control_trip_fault(app_fault_t fault);

/* ---------------- hook ---------------- */
/** @brief ADC3 注入完成入口，执行采样、保护与电流环。 */
void app_control_current_ISR(void);
/** @brief TIM1 更新入口，检测本 PWM 周期是否缺少完整电流计算。 */
void app_control_update_ISR(void);

/* ---------------- internal ---------------- */
/** @brief 仅供本模块在 PWM 停止时复位积分和反馈。 */
void app_control_reset_current(void);
/** @brief 主循环单写、ISR 单读的母线倒数与时间戳。 */
void app_control_set_bus(float inv_vbus, uint32_t sampled_ms);
/** @brief 启停 ISR 运行许可，停止后 ISR 不再开桥或写占空比。 */
void app_control_enable_current(uint8_t enable);

#endif
