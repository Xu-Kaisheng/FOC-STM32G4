# 07 Architecture and execution flow

**English** | [简体中文](../zh-CN/07-architecture-and-flow.md) | [Documentation index](../README.md)

This describes current bridge-off diagnostics and the retained v0.1 six-step path. See [driver contracts](06-driver-modules.md) and [review findings](08-review-findings.md).

## 1. Modules

- Core: generated clock/GPIO/ADC/TIM1/USART3; USER CODE application calls and shutdown.
- `app_sixstep`: modes, commutation, commands, interlock feedback, current status.
- `tim_pwm`: outputs, duty, floating phases, mode-specific interlocks, CTRL_SD, emergency stop.
- `adc_cur`: calibration, three sequential regular-group reads, complete-sample publication and failure return.
- `adc_vbus`: ADC4 calibration/conversion/scaling; [bus-voltage guide](10-bus-voltage-adc.md).
- `hall`: raw code, relative sector lookup, state text.
- `bsp_uart`: static queue, single-byte background TX, nonblocking RX.
- `bsp_perf`: DWT control timing and effective work, reported each second; [performance guide](12-performance-monitoring.md).

The application uses public APIs. Platform ADC implementations own their handles; UART alone accesses its handle. `app_board` supplies time, PWM parameters, name, and policy. There is no control ISR; ADCs are main-loop polled. G474 disables boot self-test and rejects bridge enable at application and driver layers.

## 2. Startup

```text
HAL_Init → clock → GPIO (SD low) → ADC/TIM1/USART3
  → app_init
      → initialize UART queue
      → pwm_init: bridge off, PWM stopped, duty cleared
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
  → transmit at most one queued byte
  → nonblocking receive/process of one command
  → refresh tick after any 200 ms command wait
  → advance running forward/reverse commutation at its step interval
  → Hall-monitor mode: print changes
  → every 500 ms: ADC4, then ADC1/2/3, then status logs
  → service performance reporting
```

Step interval is `floor(1000/(6×f_e))` ms; at most one step per iteration. This does not guarantee a real-time loop rate. Formatting, ADC polling, and scheduling still consume time despite nonblocking transmission.

## 4. Mode change and start

A real transition lowers SD, stops PWM, then waits 200 ms. A command for the current mode returns unchanged. G474 rejects forward/reverse. The retained locked path checks ADC readiness, clears duty, starts TIM1, checks all pairs, applies the step, checks the selected two phases, commits preload, and enables SD. Any rejection stops PWM/bridge and returns STOP; SIXSTEP is reported only after success.

Three-phase check requires `0x555`; six-step requires the selected two-phase mask. Enabled channels are not simultaneously conducting MOSFETs; TIM1 generates complementary timing and dead time.

## 5. Failures

- ADC init failure locks startup; stop/Hall monitoring remain available.
- ADC read failure stops power, latches lockout until reset, and reports failure instead of valid-looking zero current.
- PWM interlock failure ensures SD=0 and STOP.
- Fatal exception / `Error_Handler` clears MOE/AOE, outputs, and CEN, then halts without log or delay.

ADC acquisition-failure handling is not the deferred 8 A current limiter.

## 6. Invariants

Enable only after the matching interlock; disable power first on stop/fatal error. Explicitly commit preload at start/enable; never trust stale CCRs. Floating a phase must leave TIM1 running. Software ordering cannot replace hardware dead time. Keep the 200 ms wait in the main loop after PWM stops, never in an ISR. Use only successful ADC samples; calibrate Hall direction/zero before feedback. Future 20 kHz control requires renewed trigger/update/deadline checks and no ISR blocking or logging.
