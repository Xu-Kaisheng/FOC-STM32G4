/**
 * @file    alg_sixstep.c
 * @brief   六步换相纯算法：换相表与步进规则
 *
 * 不引用任何 BSP/应用符号；硬件输出只通过注入的函数指针完成。
 */
#include "alg_sixstep.h"

#include <stddef.h>

/* 六步换相表：每步给 (导通高相, 导通低相)，0=U 1=V 2=W
 * 顺序 U+V- -> U+W- -> V+W- -> V+U- -> W+U- -> W+V- */
static const uint8_t s_step_table[ALG_SIXSTEP_COUNT][2] =
{
    { ALG_SIXSTEP_PHASE_U, ALG_SIXSTEP_PHASE_V },
    { ALG_SIXSTEP_PHASE_U, ALG_SIXSTEP_PHASE_W },
    { ALG_SIXSTEP_PHASE_V, ALG_SIXSTEP_PHASE_W },
    { ALG_SIXSTEP_PHASE_V, ALG_SIXSTEP_PHASE_U },
    { ALG_SIXSTEP_PHASE_W, ALG_SIXSTEP_PHASE_U },
    { ALG_SIXSTEP_PHASE_W, ALG_SIXSTEP_PHASE_V }
};

static const alg_sixstep_io_t *s_io = NULL;

/* ---------------- init ---------------- */

/**
 * @brief  注入硬件输出回调
 * @param  io: 回调表；NULL 表示禁用输出
 */
void alg_sixstep_init(const alg_sixstep_io_t *io)
{
    s_io = io;
}

/* ---------------- read ---------------- */

/**
 * @brief  查询指定换相步的高相与低相
 * @param  step: 换相步 0~5，越界按 0 处理
 * @param  high: 高相输出地址
 * @param  low:  低相输出地址
 */
void alg_sixstep_get_phases(uint8_t step, uint8_t *high, uint8_t *low)
{
    uint8_t index = (step < ALG_SIXSTEP_COUNT) ? step : 0u;

    if (high != NULL)
    {
        *high = s_step_table[index][0];
    }
    if (low != NULL)
    {
        *low = s_step_table[index][1];
    }
}

/**
 * @brief  计算下一步的步号
 * @param  step: 当前步 0~5
 * @param  forward: 1 正转（+1），0 反转（-1）
 * @retval 下一步 0~5
 */
uint8_t alg_sixstep_next_step(uint8_t step, uint8_t forward)
{
    uint8_t current = (step < ALG_SIXSTEP_COUNT) ? step : 0u;

    if (forward != 0u)
    {
        return (uint8_t)((current + 1u) % ALG_SIXSTEP_COUNT);
    }
    return (uint8_t)((current + ALG_SIXSTEP_COUNT - 1u) % ALG_SIXSTEP_COUNT);
}

/* ---------------- write ---------------- */

/**
 * @brief  按指定步与占空比驱动三相
 * @param  step: 换相步 0~5
 * @param  duty: 高相占空比（调用方负责钳位）
 */
void alg_sixstep_apply(uint8_t step, float duty)
{
    uint8_t hi;
    uint8_t lo;
    float d[3] = { 0.0f, 0.0f, 0.0f };
    uint8_t i;

    if ((s_io == NULL) || (s_io->enable_phase == NULL) || (s_io->set_duty_raw == NULL))
    {
        return;
    }

    alg_sixstep_get_phases(step, &hi, &lo);

    d[hi] = duty;   /* 高相斩波 */
    d[lo] = 0.0f;   /* 低相上管常关，下管常通 */

    // 1) 只关掉不参与导通的相（保持另一相连续导通，避免换相时电流被打断）
    for (i = 0u; i < 3u; i++)
    {
        if ((i != hi) && (i != lo))
        {
            s_io->enable_phase(i, 0u);
        }
    }

    // 2) 写占空比
    s_io->set_duty_raw(d[ALG_SIXSTEP_PHASE_U], d[ALG_SIXSTEP_PHASE_V], d[ALG_SIXSTEP_PHASE_W]);

    // 3) 打开参与导通的两相
    s_io->enable_phase(hi, 1u);
    s_io->enable_phase(lo, 1u);
}
