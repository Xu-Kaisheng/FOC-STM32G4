/** @file    cycle.h
 *  @brief   DWT 周期计数器访问（全部 static inline，ISR 内零调用开销）
 *
 * 这里只封装"读取内核周期计数"这一硬件能力，不含任何统计逻辑与状态，
 * 因此可以被任意模块横向引用而不构成 BSP 依赖环（见 6.3 节豁免）。
 */
#ifndef CYCLE_H
#define CYCLE_H

#include <stdint.h>
#include "main.h"

#define BSP_CYCLE_US_PER_S  1000000u

/**
 * @brief  返回 CPU 主频
 * @retval Hz
 */
static inline uint32_t bsp_cycle_get_hz(void)
{
    return SystemCoreClock;
}

/**
 * @brief  读取 DWT 周期计数，天然回绕
 * @retval 当前周期计数
 */
static inline uint32_t bsp_cycle_get_now(void)
{
    return DWT->CYCCNT;
}

/**
 * @brief  周期数换算为微秒
 * @param  cycles: 周期数
 * @retval 微秒，主频为 0 时返回 0
 */
static inline float bsp_cycle_convert_us(uint32_t cycles)
{
    if (SystemCoreClock == 0u)
    {
        return 0.0f;
    }
    return ((float)cycles * (float)BSP_CYCLE_US_PER_S) / (float)SystemCoreClock;
}

/**
 * @brief  使能 DWT 周期计数器并验证它真的在走
 * @retval 1 可用；0 不可用（调试器未使能或计数停止）
 * @note   不重置共享计数器；必须在关中断或初始化阶段调用。
 */
static inline uint8_t bsp_cycle_init(void)
{
    uint32_t before;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    before = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    return (DWT->CYCCNT != before) ? 1u : 0u;
}

#endif /* CYCLE_H */
