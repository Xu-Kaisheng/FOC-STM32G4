/* 性能统计使用的 CMSIS 替身；只存在于主机测试，不进入固件工程。 */
#ifndef TEST_PERF_CLOCK_H
#define TEST_PERF_CLOCK_H
#include <stdint.h>
typedef struct { volatile uint32_t CTRL, CYCCNT; } test_dwt_t;
typedef struct { volatile uint32_t DEMCR; } test_core_debug_t;
extern test_dwt_t test_dwt;
extern test_core_debug_t test_core_debug;
extern uint32_t SystemCoreClock;
extern uint32_t test_primask;
extern uint8_t test_dwt_running;
#define DWT (&test_dwt)
#define CoreDebug (&test_core_debug)
#define CoreDebug_DEMCR_TRCENA_Msk (1u << 24u)
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define __get_PRIMASK() (test_primask)
#define __disable_irq() (test_primask = 1u)
#define __set_PRIMASK(value) (test_primask = (value))
#define __NOP() ((void)(test_dwt.CYCCNT += test_dwt_running))
#endif
