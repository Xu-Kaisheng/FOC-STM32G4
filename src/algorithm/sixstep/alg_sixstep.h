/**
 * @file    alg_sixstep.h
 * @brief   六步换相纯算法：换相表与步进规则
 *
 * 本模块不 include 任何 BSP/应用头文件，也不直接访问寄存器。
 * 需要写 PWM 时通过调用方注入的回调完成（依赖倒置），
 * 因此可以脱离硬件单独测试，也可以被不同的板级实现复用。
 */
#ifndef ALG_SIXSTEP_H
#define ALG_SIXSTEP_H

#include <stdint.h>

/* ---------------- 公共宏 ---------------- */

#define ALG_SIXSTEP_PHASE_U  0u
#define ALG_SIXSTEP_PHASE_V  1u
#define ALG_SIXSTEP_PHASE_W  2u
#define ALG_SIXSTEP_COUNT    6u

/* ---------------- 公共类型 ---------------- */

/* 硬件输出回调；由应用层注入，算法层不关心具体实现。 */
typedef struct
{
    uint8_t (*apply_pair)(uint8_t high, uint8_t low);
    void (*set_duty_raw)(float du, float dv, float dw);
} alg_sixstep_io_t;

/* ---------------- init ---------------- */

/**
 * @brief  注入硬件输出回调
 * @param  io: 回调表；传 NULL 表示禁用输出（apply 返回失败）
 */
void alg_sixstep_init(const alg_sixstep_io_t *io);

/* ---------------- read ---------------- */

/**
 * @brief  查询指定换相步的高相与低相
 * @param  step: 换相步 0~5，越界按 0 处理
 * @param  high: 高相输出地址（斩波相）
 * @param  low:  低相输出地址（下管常通相）
 */
void alg_sixstep_get_phases(uint8_t step, uint8_t *high, uint8_t *low);

/**
 * @brief  计算下一步的步号
 * @param  step: 当前步 0~5
 * @param  forward: 1 正转（+1），0 反转（-1）
 * @retval 下一步 0~5
 */
uint8_t alg_sixstep_next_step(uint8_t step, uint8_t forward);

/* ---------------- write ---------------- */

/**
 * @brief  按指定步与占空比驱动三相
 * @param  step: 换相步 0~5
 * @param  duty: 高相占空比（调用方负责钳位）
 * @retval 1 输出提交成功；0 回调缺失或驱动拒绝，调用方必须停机
 * @note   三相预先维护同一占空比；回调一次提交高相斩波、低相常通及第三相悬空。
 */
uint8_t alg_sixstep_apply(uint8_t step, float duty);

#endif /* ALG_SIXSTEP_H */
