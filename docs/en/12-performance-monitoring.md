# 12 Control timing and CPU workload

**English** | [简体中文](../zh-CN/12-performance-monitoring.md) | [Documentation index](../README.md)

## Function and definitions

After boot diagnostics the main loop saves the latest DWT accounting window every 1000 ms. Send `p` to print it once. Repeated reads within a window use the same snapshot. A request before the first window completes or after an invalid clock sample reports `[CPU] unavailable (DWT/clock/window)`. Time conversion uses the runtime `SystemCoreClock`.

| Field | Meaning |
|---|---|
| `TASK` | Exclusive time spent in foreground tasks, with interrupt preemption removed |
| `IRQ` | Time spent in interrupts, accounting for nested interrupts separately |
| `POLL` | Empty polling, dispatch checks and unassigned time while the CPU is running |
| `CURRENT` | Average and maximum current-loop duration and count of calls reaching 50 µs. OFF indicates an empty sample window. |

The two columns break down individual tasks and interrupts. For example, `LOG` measures queue reclamation and DMA submission, `STATUS` measures telemetry encoding and `PERF` includes formatting of previous reports. Polling for DMA completion and incoming characters is included in `POLL`. Control-call duration includes preemption for deadline analysis.

## Example output

The values below illustrate the format.

```text
[CPU] TASK 2.00%  IRQ 18.00%  POLL 80.00%
  TASK        CPU    IRQ         CPU
  CMD        0.00%   ADC3      17.00%
  CONTROL    1.50%   TIM1       0.75%
  HALL       0.00%   SYSTICK    0.25%
  LOG        0.25%   USART3     0.00%
  STATUS     0.25%   DMA_TX     0.00%
  PERF       0.00%
  CURRENT  avg= 8.50us  max=12.00us  over=0
```

Average, maximum, and overruns reset each window, not only at boot. The nonblocking UART queue can drop whole messages; logs are not lossless tracing.

## API and call order

| API | Contract |
|---|---|
| `app_perf_init()` | After startup diagnostics; enable/check DWT without depending on debugger setup |
| `app_perf_begin_work()` / `app_perf_end_work()` | Mark useful work; supports nesting; pair every exit |
| `app_perf_enter_irq()` / `app_perf_exit_irq()` | Pair in USER CODE interrupt entry/exit; nested IRQ time is not double-counted |
| `app_perf_begin_control()` | Return start cycles and enter useful-work interval |
| `app_perf_end_control(id,start)` | Accumulate category and leave interval; a category must not recurse |
| `app_perf_get_snapshot(out)` | Consume/reset one window in main loop (read-and-clear); normally owned by `app_perf_task()` |
| `app_perf_task()` | Called each main iteration; throttled by tick; formats only in main loop |

## Real-time limits

- Interrupt hooks use cycle reads and integer accounting, without printing, floating-point division, or waiting.
- Short PRIMASK critical sections protect nested-depth/counter updates and snapshots, restoring previous mask state. Algorithms/formatting stay outside. Measure overhead and interrupt latency; this is not zero-intrusion instrumentation.
- Exception entry/exit and hook boundaries are not fully counted. New uninstrumented handlers would be omitted. Fatal exceptions prioritize power shutdown, not reporting.
- Unsigned subtraction handles one wrap. DWT wraps at ~25.26 s at 170 MHz; normal windows are ~1 s. Windows exceeding 10 s are discarded to avoid false results after multiple wraps.
- Lost DWT trace/counter enables, a stopped counter, zero cycle windows, or clock changes report `[CPU] unavailable (DWT/clock/window)`, not a false 0%. The main loop then reinitializes timing and accounting, discards the affected window, and resumes accounting from a complete new window one second later. Persistent failures retry once per second; recovery never runs in an ISR or resets the shared CYCCNT. Debugger pauses, low-power intervals, or external DWT modifications invalidate timing conclusions.
- Accounting does not alter clock/pins/triggers/PWM policy/protection thresholds, and cannot replace synchronized sampling or fast protection.

## Validation

1. Host tests cover union/nesting/wrap/window reset, work spanning windows, the 50 µs boundary, no samples, DWT failure, long windows, and clock changes.
   Runtime tests also exercise real `app_task()` dispatch to verify empty receive polling, command work, DMA submission, rejected submissions and reclamation-only exits. The actual interrupt source is compiled to verify DMA/USART coverage and nested SysTick union accounting. Injected test cycles are not measurements of hardware hook overhead.
2. Rebuild G474 with zero errors/warnings. Core interrupt hooks remain in USER CODE; the Keil project includes the module.
3. With the bridge off, send `p` after the first second and check for one table with `CURRENT  OFF`. After a mode change, the 200 ms bridge reset wait contributes to `CMD` in the corresponding window.
4. Later compare GPIO markers and scope timing against maxima, measuring hook/critical-section overhead and real overrun behavior under load.
