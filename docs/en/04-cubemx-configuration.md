# 04 CubeMX configuration maintenance

**English** | [简体中文](../zh-CN/04-cubemx-configuration.md) | [Documentation index](../README.md)

This page owns configuration sources and regeneration, not copies of wiring tables or runtime policy.

## Configuration sources

| Content | Source |
|---|---|
| Clock, GPIO, timer, ADC, DMA, UART | [G474-FOC.ioc](../../G474-FOC.ioc) |
| Toolchain, device pack, sources and include paths | [Keil project](../../MDK-ARM/G474-FOC.uvprojx) |
| Runtime board validation parameters | [board.c](../../src/bsp/board/board.c) |
| TIM1 mode commits and enables | [PWM contracts](06-driver-modules.md) |
| ADC4 sequence and reference acquisition | [Bus/VREF](10-bus-voltage-adc.md) |

Check generated values separately from runtime overrides. PWM startup/shutdown manages AOE, CCPC and COM; CubeMX initial modes do not establish running outputs. Pin assignments belong to [01](01-hardware-and-wiring.md), alternate-function/shared-net evidence to [11](11-g474-board-guide.md).

## Regeneration

1. Modify `.ioc` and regenerate through CubeMX. Do not hand-edit Core outside USER CODE or HAL/CMSIS.
2. Preserve application entry, emergency shutdown, IRQ accounting hooks and Keil App/Algo/BSP source/include entries.
3. CubeMX owns UART DMA/NVIC setup; BSP must not independently patch clock setup. Do not change compiler options for a local installation.
4. Review generated differences and retained user code, then build, run regression and structure checks via [09](09-validation-and-maintenance.md).
5. Clock, pin, trigger or output timing changes require the affected module’s hardware checks. Configuration is not measurement.
