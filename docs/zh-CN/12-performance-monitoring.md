# 12 控制耗时与 CPU 负载统计

[English](../en/12-performance-monitoring.md) | **简体中文** | [文档目录](../README.md)

## 功能与统计口径

统计逻辑位于 `src/app/perf/perf.c/.h` 与 `perf_print.c`，DWT 寄存器访问下沉到 `src/bsp/time/cycle.h`（全部 `static inline`）；这样 `bsp/uart` 不再自己做自计时，日志推进移到 `app/log`，依赖环被破除。通过 Cortex-M 的 DWT CYCCNT 记录 CPU 周期，每 1000 ms 从主循环输出。换算使用运行时 `SystemCoreClock`，当前为 170 MHz 配置。初始化在 `app_init()` 末尾，因此 ADC 启动校准不混入运行窗口。

当前没有电流闭环，不能把低频 ADC 诊断称为电流环。G474 禁止开桥与上电 PWM 自检；临时开机 demo 已完成测试并关闭，启动诊断结束后初始化性能统计，因此正常运行时两个控制统计均显示 `OFF (no samples)`。未来解锁并运行六步模式后，`SIXSTEP` 显示真实换相输出函数耗时。

| 日志字段 | 含义 |
|---|---|
| `CPU work` | 窗口内已标记有效工作与已接入中断的时间并集 / 窗口总周期数，是有效负载估算 |
| `busy` | 上述有效工作耗时，ms；包括命令中的 200 ms 忙等和 ADC 转换轮询 |
| `poll` | 窗口剩余时间，ms；包括空轮询、调度检查与未覆盖的计时开销，不是硬件休眠 |
| `irq` | 已接入中断区间耗时，ms；属于 busy 的子集，不能再与 busy 相加 |
| `win` | 实际统计窗口耗时，ms；主循环阻塞时可能超过 1000 ms，不假定窗口固定为一秒 |
| `SIXSTEP n/avg/max/share` | `app_apply_step()` 调用次数、平均/最大耗时（us）、累计耗时占窗口的比例；不是 20 kHz 电流环 |
| `CURRENT n/avg/max/share` | 电流环统计接口的同类结果；当前无调用，显示 OFF |
| `CURRENT budget/peak/over` | 电流环预算 50 us、最大耗时占预算比例、耗时大于或等于 50 us 的次数；仅有样本时输出 |

**CPU 目前一直执行轮询，没有启用 WFI 休眠，物理运行占比仍接近 100%。** `work` 不包含所有执行过的指令，不能当作功耗、RTOS 空闲率或严格的剩余算力证明。它排除了无业务的轮询，用于比较诊断、控制、命令处理的有效工作负担。当前覆盖：命令处理、六步输出、霍尔变化日志、周期 ADC 诊断及日志、实际串口发送字节、统计格式化及 SysTick。未单独覆盖的接收/调度检查开销归入 poll。

控制任务测得的是含中断抢占的经过时间，反映响应期限；因此其 share 与 irq 可能重叠，不能相加。六步调度不是每 50 us 执行一次，不套用电流环预算。`OFF (no samples)` 表示该窗口没有控制函数调用；不代表验证了硬件输出一定关闭。

## 串口示例

以下为格式示例，数字不是实板测量值：

```text
[PERF] CPU work=0.50% busy=5.000ms poll=995.000ms irq=0.100ms win=1000.000ms
[PERF] SIXSTEP=OFF (no samples)
[PERF] CURRENT=OFF (no samples)
```

以后真正接入电流环后，才会出现例如：

```text
[PERF] CURRENT n=20000 avg=10.00us max=12.00us share=20.00%
[PERF] CURRENT budget=50us peak=24.00% over=0
```

平均和最大值、超时次数均按当前窗口清零，不是上电以来的历史最大值。日志走现有非阻塞队列；队列满仍可能丢整条日志，不能把串口日志当作无损实时追踪。

## 接口与调用时序

| 接口 | 调用约束 |
|---|---|
| `app_perf_init()` | 启动诊断完成后调用；启用并检查 DWT 是否计数，不要求调试器先开启 |
| `app_perf_begin_work()` / `app_perf_end_work()` | 标记有效工作，支持嵌套；所有出口必须配对 |
| `app_perf_enter_irq()` / `app_perf_exit_irq()` | 正常 ISR 的 USER CODE 入口/出口配对；嵌套中断不重复累计 |
| `app_perf_begin_control()` | 返回开始周期，同时进入有效工作区间 |
| `app_perf_end_control(id, start)` | 完成指定控制任务统计并退出工作区间；每种控制任务不可递归重入 |
| `app_perf_get_snapshot(out)` | 主循环消费一次窗口并清零（read-and-clear）；正常应用由 `app_perf_task()` 独占调用 |
| `app_perf_task()` | 每轮主循环调用，内部按毫秒时基限频；格式化只在主循环进行 |

未来电流环的真实计算入口调用 `app_perf_begin_control()`，在计算及 PWM 占空比提交完成后调用 `app_perf_end_control(APP_PERF_CURRENT, start)`，同时为新增 ADC/定时器 ISR 接入中断统计。当前没有增加假的电流环调用，也没有为凑出统计值启动 PWM。

## 实时性与限制

- 中断只做周期读数和整数累计，不打印、不浮点除法、不等待。
- 为避免主循环与嵌套 ISR 同时修改深度、累计值，统计状态更新及窗口复制使用短 PRIMASK 临界区，并恢复调用者原屏蔽状态；没有把控制算法或格式化放进临界区。它会引入少量延迟，实板需测量开销，不能声称零扰动。
- 硬件异常入栈/退栈、钩子边界和主循环调度并未全部计入 busy；未接入钩子的新增中断会漏计，因此增添中断时必须同步接入。致命异常仍优先关桥，不尝试继续报告性能。
- 无符号周期差可处理一次自然回绕；170 MHz 时 DWT 约 25.26 s 回绕。正常窗口约 1 s，超过 10 s 的窗口丢弃并报告 unavailable，避免多次回绕伪造结果。
- DWT 不计数、窗口周期差为零或运行中 `SystemCoreClock` 改变时，报告 `[PERF] unavailable (DWT/clock/window)`，不报 0% 假结果。改频后应重新初始化统计。暂停调试、低功耗以及外部工具改写 DWT 的时间段不用于性能结论。
- 本实现不改变时钟、引脚、ADC 触发、PWM 策略或保护门限；不能用耗时统计代替电流环、同步采样或快速保护。

## 验证方法

1. 主机回归覆盖时间并集、嵌套中断、计数回绕、快照清零及跨窗工作、50 us 预算临界值、无样本提示、DWT 失效、长窗口和改频拒绝发布。
2. 编译唯一 G474 Keil 目标，检查 0 error / warning；生成文件的修改仅限 ISR 的 USER CODE，工程文件只增加源文件条目。
3. 实板先保持关桥，确认每秒出现 PERF 日志，G474 的 CURRENT/SIXSTEP 无样本；发送模式命令时，200 ms 忙等会使该窗口 busy 明显上升。
4. 后续启用控制时，用 GPIO 翻转及示波器对照最大耗时，检查统计钩子和临界区的实际开销，并用真实负载验证超时次数。

2026-11-16 验证结果：

| 检查 | 结果 |
|---|---|
| G474 / ARMCC | 0 Error(s)、0 Warning(s)；Code 28140、RO 1492、RW 104、ZI 4496 字节 |
| 主机回归 | 原运行/故障联锁、G474 平台、性能计时三组均通过 |
| 生成区与工程选项 | 与本次修改前逐项对比，ISR 非 USER CODE 区域和G474 的 TargetOption 均未改变；原有源码注释保留 |

本次尚未烧录或取得实板性能数据。
