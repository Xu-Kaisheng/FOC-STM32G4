/* 提供可手动推进的 DWT 与中断屏蔽状态，用于主机回归。 */
#include "perf_clock.h"
test_dwt_t test_dwt;
test_core_debug_t test_core_debug;
uint32_t SystemCoreClock = 170000000u;
uint32_t test_primask;
uint8_t test_dwt_running = 1u;
