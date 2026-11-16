/** @file bsp_perf.h
 *  @brief DWT 周期计时与有效工作负载统计；空轮询不计入有效工作。
 */
#ifndef BSP_PERF_H
#define BSP_PERF_H
#include <stdint.h>

#define BSP_PERF_REPORT_MS         1000u
#define BSP_PERF_CURRENT_PERIOD_US 50u

typedef enum
{
    BSP_PERF_SIXSTEP = 0,
    BSP_PERF_CURRENT,
    BSP_PERF_CONTROL_COUNT
} bsp_perf_control_t;

typedef struct
{
    uint32_t count;
    uint32_t total_cycles;
    uint32_t max_cycles;
    uint32_t overruns;
} bsp_perf_control_stats_t;

typedef struct
{
    uint32_t clock_hz;
    uint32_t elapsed_cycles;
    uint32_t work_cycles;     /* 前台有效工作与中断的时间并集，不重复计数。 */
    uint32_t irq_cycles;      /* work_cycles 的子集。 */
    bsp_perf_control_stats_t control[BSP_PERF_CONTROL_COUNT];
} bsp_perf_snapshot_t;

/** @brief 初始化计数器；在外设和启动自检完成后、主循环开始前调用。 */
void bsp_perf_init(void);
/** @brief 标记有效工作开始；支持前台与中断嵌套，必须配对结束。 */
void bsp_perf_work_begin(void);
/** @brief 标记有效工作结束；忙等延时也消耗 CPU，应包含在工作区间。 */
void bsp_perf_work_end(void);
/** @brief 在正常 ISR 的 USER CODE 入口调用；不用于致命异常。 */
void bsp_perf_irq_enter_ISR(void);
/** @brief 在正常 ISR 的 USER CODE 出口调用，与入口配对。 */
void bsp_perf_irq_exit_ISR(void);
/** @brief 开始控制任务计时并进入有效工作区间，返回周期时间戳。 */
uint32_t bsp_perf_control_begin(void);
/** @brief 结束控制计时；含抢占时间，CURRENT 按 50 us 预算统计超时。 */
void bsp_perf_control_end(bsp_perf_control_t control, uint32_t start);
/** @brief 主循环取出并清零窗口；失败返回 0，不发布伪造的零负载。 */
uint8_t bsp_perf_snapshot(bsp_perf_snapshot_t *out);
/** @brief 主循环每秒输出一次；无控制调用明确显示 OFF。 */
void bsp_perf_task(void);
#endif
