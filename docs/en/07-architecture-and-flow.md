# 07 Architecture and application flow

**English** | [简体中文](../zh-CN/07-architecture-and-flow.md) | [Documentation index](../README.md)

This page owns responsibilities and call relationships. [AGENTS](../../AGENTS.md) owns development rules, [09](09-validation-and-maintenance.md) owns operation and drive permission, and [06](06-driver-modules.md) owns module contracts.

## Layers

```text
src/app/             Orchestration, commands, control policy, status, logs, performance
src/algorithm/       Pure algorithms with injected output callbacks, no HAL/app dependency
src/bsp/             Board parameters and hardware primitives
src/third_party/     External implementations
Core/                Generated initialization; application/exception hooks in USER CODE
Drivers/             Read-only HAL/CMSIS
```

Dependencies flow from app to algorithm, BSP or third_party, never upward. BSP depends on its own interfaces and board, with the inline timebase exception; pwm.c orchestrates primitives within the PWM module. BSP contains no tasks.

| Application module | Responsibility |
|---|---|
| app/app | Initialization, mode switching and main loop; app_state encapsulates shared state |
| app/cmd | Nonblocking single-character command dispatch |
| app/control | Policy parameters, step state and scheduling; BSP callback injection |
| app/status | Slow acquisition and reporting |
| app/log | Static queue and DMA progress |
| app/perf | Work/IRQ/control accounting and main-loop reporting |

Use public functions across modules. ADC handles stay in BSP; algorithms do not access HAL. Keil groups are App, Algo and BSP, with matching include paths.

## Startup and scheduling

`Core main → app_init → app_task`. Startup prepares UART and parameters, initializes PWM with the bridge off, calibrates current/bus ADCs, validates a bus read and initializes Hall input. Any optional waveform self-test keeps the bridge off. Performance initialization follows startup logging so calibration is outside normal accounting.

The main loop advances log transmission, commands, control, mode-dependent Hall monitoring, status and performance reporting in that order. There is no control ISR; future interrupt control requires its own timing design.

## Mode transitions

Requests for the current mode do not restart it. A real transition disables the bridge/PWM and waits for latch reset. Startup checks board permission, ADC readiness and PWM interlocks; rejection returns STOP. Commands, delay and policy parameters belong to [09](09-validation-and-maintenance.md).

Six-step flow is `app_control_task → alg_sixstep_next_step / alg_sixstep_apply → injected BSP callbacks`. Algorithms choose phase roles, BSP commits them. COM/UEV semantics belong to [06](06-driver-modules.md).

## Failure boundaries

- ADC failure or stale samples disable power and latch startup lockout until reset/calibration. Old samples are not new measurements.
- Failed output submission/interlock stops PWM/power and returns STOP.
- Fatal exceptions invoke BSP emergency shutdown from Core USER CODE, without logging or delay dependencies.
- Log and timing-source recovery do not automatically enable power. See [06](06-driver-modules.md) and [12](12-performance-monitoring.md).

`tests/check_structure.sh` checks structure and naming. Build/regression/hardware procedures belong to [09](09-validation-and-maintenance.md).
