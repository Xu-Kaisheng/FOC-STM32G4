/** @file    perf.h
 *  @brief   DWT 周期计时与有效工作负载统计；空轮询不计入有效工作。
 *
 * 从原性能统计模块拆分而来：
 *   - 硬件能力（读周期计数）下沉到 bsp/time/cycle.h；
 *   - 统计与业务分类（CURRENT）留在应用层，BSP 不再被业务牵着走。
 */
#ifndef PERF_H
#define PERF_H

#include <stdint.h>

/* ---------------- 公共宏 ---------------- */

#define APP_PERF_REPORT_MS         1000u
#define APP_PERF_CURRENT_PERIOD_US 50u

/* ---------------- 公共类型 ---------------- */

typedef enum
{
    APP_PERF_CURRENT = 0,
    APP_PERF_CONTROL_COUNT
} app_perf_control_t;

typedef enum
{
    APP_PERF_TASK_CMD = 0,
    APP_PERF_TASK_CONTROL,
    APP_PERF_TASK_HALL,
    APP_PERF_TASK_LOG,
    APP_PERF_TASK_STATUS,
    APP_PERF_TASK_PERF,
    APP_PERF_TASK_COUNT
} app_perf_task_t;

typedef enum
{
    APP_PERF_IRQ_ADC = 0,
    APP_PERF_IRQ_PWM,
    APP_PERF_IRQ_SYSTICK,
    APP_PERF_IRQ_UART,
    APP_PERF_IRQ_DMA,
    APP_PERF_IRQ_COUNT
} app_perf_irq_t;

typedef struct
{
    uint32_t count;
    uint32_t total_cycles;
    uint32_t max_cycles;
    uint32_t overruns;
} app_perf_control_stats_t;

typedef struct
{
    uint32_t clock_hz;
    uint32_t elapsed_cycles;
    uint32_t work_cycles;     /* 前台有效工作与中断的时间并集，不重复计数。 */
    uint32_t irq_cycles;      /* work_cycles 的子集。 */
    uint32_t task_cycles[APP_PERF_TASK_COUNT]; /* 各前台任务的独占时间，已扣除 IRQ。 */
    uint32_t irq_by_source[APP_PERF_IRQ_COUNT]; /* 嵌套 IRQ 分别归属，不重复计数。 */
    app_perf_control_stats_t control[APP_PERF_CONTROL_COUNT];
} app_perf_snapshot_t;

/* ---------------- init ---------------- */

/** @brief 初始化计数器；在外设和启动自检完成后、主循环开始前调用。 */
void app_perf_init(void);

/* ---------------- task ---------------- */

/** @brief 由 app_task 每秒调用输出；无控制样本显示 OFF，失效后重建统计窗口。 */
void app_perf_task(void);

/* ---------------- read ---------------- */

/**
 * @brief  取出窗口快照并把窗口累加器清零（read-and-clear）
 * @param  out: 输出快照
 * @retval 1 数据有效；0 失败（DWT/时钟/窗口异常），不发布伪造的零负载
 * @note   本函数在返回数据的同时清零窗口，属于 read-and-clear 例外：
 *         如果拆成"读快照 + 单独清零"两次调用，中间会插入采样而丢失计数。
 */
uint8_t app_perf_get_snapshot(app_perf_snapshot_t *out);

/* ---------------- hook ---------------- */

/** @brief 标记有效工作开始；支持前台与中断嵌套，必须配对结束。 */
void app_perf_begin_work(void);
/** @brief 标记有效工作结束；忙等延时也消耗 CPU，应包含在工作区间。 */
void app_perf_end_work(void);
/** @brief 标记前台任务有效工作；任务之间不嵌套，IRQ 抢占时间自动扣除。 */
void app_perf_begin_task(app_perf_task_t task);
/** @brief 结束前台任务计时，与 begin_task 配对。 */
void app_perf_end_task(void);
/** @brief 在正常 ISR 的 USER CODE 入口指定来源；不用于致命异常。 */
void app_perf_enter_irq(app_perf_irq_t source);
/** @brief 在正常 ISR 的 USER CODE 出口调用，与入口配对。 */
void app_perf_exit_irq(void);
/** @brief 开始控制任务计时并进入有效工作区间，返回周期时间戳。 */
uint32_t app_perf_begin_control(void);
/** @brief 结束控制计时；含抢占时间，CURRENT 按 50 us 预算统计超时。 */
void app_perf_end_control(app_perf_control_t control, uint32_t start);

#endif /* PERF_H */
