# 07 Architecture and execution flow

**English** | [简体中文](../zh-CN/07-architecture-and-flow.md) | [Documentation index](../README.md)

This describes current bridge-off diagnostics and the retained v0.1 six-step path. See [driver contracts](06-driver-modules.md) and [review findings](08-review-findings.md).

## 1. Modules

- Core: generated clock/GPIO/ADC/TIM1/USART3; USER CODE application calls and shutdown.
- `app/app`: `app` (init, mode interlocks, main loop), `app_state` (mode, timestamps, hall, ADC availability).
- `app/cmd`: `app_cmd_task()`: nonblocking command read and parsing.
- `app/control`: algorithm strategy: six-step parameters, bench limits, BSP callback injection.
- `app/log`: `app_log_task()`: log formatting, 512-byte queue and DMA pumping.
- `app/status`: `app_status_task()`: periodic status output.
- `algorithm/sixstep`: step table and progression, pure algorithm; hardware output via injected callbacks.
- `algorithm/foc`, `third_party`: reserved.
- `bsp/pwm`: outputs, duty, floating phases, mode-specific interlocks, CTRL_SD, emergency stop.
- `adc_cur`: calibration, three sequential regular-group reads, complete-sample publication and failure return.
- `adc_vbus`: ADC4 calibration/conversion/scaling; [bus-voltage guide](10-bus-voltage-adc.md).
- `hall`: raw code, relative sector lookup, state text.
- `bsp/uart`: USART3 hardware only: start one TX DMA transfer, query busy, nonblocking RX; **no queue, no task**.
- `bsp/board`: board constants and time base; the only BSP module others may depend on.
- `app/perf` + `bsp/time`: DWT control timing and effective work, reported each second; [performance guide](12-performance-monitoring.md).

If the applications call down only: applications may use `algorithm`, `third_party` and `bsp`; `algorithm`/`third_party` never include app/bsp; `bsp` depends only on `bsp/board` and contains no `*_task()`. UART alone accesses its handle. There is no control ISR; ADCs are main-loop polled. G474 disables boot self-test and rejects bridge enable at application and driver layers.

## 2. Startup

```text
HAL_Init → clock → GPIO (SD low) → ADC/TIM1/USART3
  → app_init
      → app_log_init (512-byte log queue)
      → bsp_pwm_init: bridge off, PWM stopped, duty cleared
      → calibrate ADC1/2/3; lock start on failure
      → calibrate ADC4, read VBUS; lock start on failure
      → initialize Hall inputs
      → skip disabled boot PWM self-test
      → print startup state; initialize performance counters
  → while (1): app_task
```

Any self-test requires SD=0. Configuration failure never authorizes bypassing checks. Initial mode is STOP.

## 3. Main loop

```text
app_task
  → app_log_task()        background log TX (DMA)
  → app_cmd_task()        nonblocking one-command read and parse
  → app_control_task()    six-step advance at the step interval (algorithm + BSP callbacks)
  → Hall monitor prints only on state change
  → app_status_task()     every 500 ms read ADC4/ADC1-3 and print status
  → app_perf_task()       once-per-second performance report
```

Step interval is `floor(1000/(6×f_e))` ms; at most one step per iteration. This does not guarantee a real-time loop rate. Formatting, ADC polling, and scheduling still consume time despite nonblocking transmission.

## 4. Mode change and start

A real transition lowers SD, stops PWM, then waits 200 ms. A command for the current mode returns unchanged. G474 rejects forward/reverse. The retained locked path checks ADC readiness, prepares equal duties, starts TIM1 with PWM1 modes and CCR values committed, checks all pairs, then calls `alg_sixstep_apply`. Its application-injected callback commits high-phase PWM1, low-phase forced inactive and the third phase disabled with one COM. The enable interlock checks the exact modes/pairs and equal nonzero CCRs, commits preloads, then enables SD. Any rejection stops PWM/bridge and returns STOP; SIXSTEP is reported only after success.

Three-phase check requires `0x555`; six-step requires the selected two-phase mask. Enabled channels are not simultaneously conducting MOSFETs; TIM1 generates complementary timing and dead time.

Running commutation changes output roles through COM without resetting the counter; duty updates use UEV to transfer a complete set. Submission failure immediately stops power/PWM and returns STOP. Output APIs require one main-loop writer; see the [PWM driver](06-driver-modules.md) for constraints and validation.

## 5. Failures

- ADC init failure locks startup; stop/Hall monitoring remain available.
- ADC read failure stops power, latches lockout until reset, and reports failure instead of valid-looking zero current.
- PWM interlock failure ensures SD=0 and STOP.
- Fatal exception / `Error_Handler` clears MOE/AOE, outputs, and CEN, then halts without log or delay.

ADC acquisition-failure handling is not the deferred 8 A current limiter.

## 6. Invariants

Enable only after the matching interlock; disable power first on stop/fatal error. Explicitly commit preload at start/enable; never trust stale CCRs. Floating a phase must leave TIM1 running. Software ordering cannot replace hardware dead time. Keep the 200 ms wait in the main loop after PWM stops, never in an ISR. Use only successful ADC samples; calibrate Hall direction/zero before feedback. Future 20 kHz control requires renewed trigger/update/deadline checks and no ISR blocking or logging.
