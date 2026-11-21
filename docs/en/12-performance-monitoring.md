# 12 Control timing and CPU workload

**English** | [简体中文](../zh-CN/12-performance-monitoring.md) | [Documentation index](../README.md)

## Function and definitions

Accounting lives in `src/app/perf/perf.c/.h` plus `perf_print.c`; DWT register access moved down to `src/bsp/time/cycle.h` (all `static inline`), which also removes the old `bsp/uart` self-timing dependency cycle; log pumping now lives in `app/log`. It uses Cortex-M DWT CYCCNT and reports from the main loop every 1000 ms. Conversion uses runtime `SystemCoreClock`, currently 170 MHz. Initialization at the end of `app_init()` excludes startup ADC calibration from runtime windows.

There is no current loop. Slow ADC diagnostics are not current control. G474 disables power enable and boot self-test; the temporary PWM demo is finished and disabled. Both control categories normally report `OFF (no samples)`. A future unlocked six-step path would report actual `app_apply_step()` time.

| Field | Meaning |
|---|---|
| `CPU work` | Union of marked useful work and instrumented interrupt intervals divided by total window cycles; effective workload estimate |
| `busy` | Union duration in ms, including command 200 ms busy waits and ADC polling |
| `poll` | Remaining window time: empty polling, scheduling checks, uncovered accounting overhead; not hardware sleep |
| `irq` | Instrumented interrupt time; a subset of busy, do not add it again |
| `win` | Actual window duration; blocking may extend it beyond 1000 ms |
| `SIXSTEP n/avg/max/share` | `app_apply_step()` count, average/maximum µs, accumulated time/window; not a 20 kHz current loop |
| `CURRENT n/avg/max/share` | Equivalent current-loop API results; no calls today, so OFF |
| `CURRENT budget/peak/over` | 50 µs budget, maximum/budget ratio, count of durations ≥50 µs; only printed with samples |

**The CPU continuously polls; WFI sleep is not enabled, so physical active time is near 100%.** `work` does not include every executed instruction and is not a power estimate, RTOS idle metric, or proof of spare compute capacity. It separates instrumented useful work from empty polling to compare diagnostic/control/command workloads.

Coverage includes commands, six-step output, Hall-change logs, periodic ADC/log work, actual UART byte writes, performance formatting, and SysTick. Uninstrumented receive/scheduling checks fall into poll. Control elapsed time includes preemption, reflecting deadlines; its share may overlap IRQ time and must not be added to it. Six-step scheduling is not every 50 µs, so it does not use the current-loop budget. OFF means no calls in the window, not independent proof of hardware output state.

## Example output

These values illustrate formatting, not measured hardware results:

```text
[PERF] CPU work=0.50% busy=5.000ms poll=995.000ms irq=0.100ms win=1000.000ms
[PERF] SIXSTEP=OFF (no samples)
[PERF] CURRENT=OFF (no samples)
```

After a real current loop is integrated, output could look like:

```text
[PERF] CURRENT n=20000 avg=10.00us max=12.00us share=20.00%
[PERF] CURRENT budget=50us peak=24.00% over=0
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

Future current control should bracket real computation and PWM-duty submission with begin/end using `APP_PERF_CURRENT`; add IRQ hooks to new ADC/timer handlers. No fake control call or PWM start was added merely to create measurements.

## Real-time limits

- Interrupt hooks use cycle reads and integer accounting, without printing, floating-point division, or waiting.
- Short PRIMASK critical sections protect nested-depth/counter updates and snapshots, restoring previous mask state. Algorithms/formatting stay outside. Measure overhead and interrupt latency; this is not zero-intrusion instrumentation.
- Exception entry/exit and hook boundaries are not fully counted. New uninstrumented handlers would be omitted. Fatal exceptions prioritize power shutdown, not reporting.
- Unsigned subtraction handles one wrap. DWT wraps at ~25.26 s at 170 MHz; normal windows are ~1 s. Windows exceeding 10 s are discarded to avoid false results after multiple wraps.
- Non-counting DWT, zero cycle windows, or clock changes report `[PERF] unavailable (DWT/clock/window)`, not a false 0%. Reinitialize after changing clocks. Debugger pauses, low-power intervals, or external DWT modifications invalidate timing conclusions.
- Accounting does not alter clock/pins/triggers/PWM policy/protection thresholds, and cannot replace synchronized sampling or fast protection.

## Validation

1. Host tests cover union/nesting/wrap/window reset, work spanning windows, the 50 µs boundary, no samples, DWT failure, long windows, and clock changes.
2. Rebuild G474 with zero errors/warnings. Core interrupt hooks remain in USER CODE; the Keil project includes the module.
3. With bridge off, check one-second PERF logs and CURRENT/SIXSTEP OFF. A mode transition's 200 ms wait increases busy in its window.
4. Later compare GPIO markers and scope timing against maxima, measuring hook/critical-section overhead and real overrun behavior under load.

Recorded 2026-11-16 software validation: ARMCC produced 0 errors/warnings (Code 28140, RO 1492, RW 104, ZI 4496 bytes); runtime/fault, G474, and performance suites passed. Non-USER-CODE interrupt regions and target compiler options were unchanged for that feature. No hardware performance measurements have been collected.
