# 04 CubeMX configuration

**English** | [简体中文](../zh-CN/04-cubemx-configuration.md) | [Documentation index](../README.md)

The only configuration source is `G474-FOC.ioc`. Tools: CubeMX 6.16.1, STM32CubeG4 v1.6.3, STM32G4xx_DFP 1.5.0, and Keil ARM Compiler 5.06 update 7.

## Current configuration

| Peripheral | Configuration |
|---|---|
| MCU | STM32G474RET6, LQFP64 |
| RCC | HSI 16 MHz, PLL M=4/N=85/R=2, SYSCLK 170 MHz |
| TIM1 | PSC=0, ARR=4250, RCR=1; center-aligned mode 1; PWM1; active-high main/complementary outputs |
| BDTR | Clock divider 1, DTG=149, 1 µs dead time; OSSI/OSSR enabled, AOE disabled |
| GPIO | PC9 output initially low; PC6/PC7/PC8 inputs with pull-ups |
| ADC1 | PC0 / IN6, phase U |
| ADC2 | PC1 / IN7, phase V |
| ADC3 | PB1 / IN1, phase W |
| ADC4 | PB12 / IN3, bus divider |
| ADC common | Independent mode; SYSCLK/4=42.5 MHz; 12-bit right-aligned; 247.5 sampling cycles; one regular channel per ADC; software trigger |
| USART3 | PB10 TX / PB11 RX, 115200 8N1 |
| Debug | PA13 SWDIO / PA14 SWCLK |

PA8/PA9/PA10 are TIM1_CH1/2/3 at AF6. PB13/PB14 are CH1N/2N at AF6; **PB15 CH3N uses AF4**. See [hardware](01-hardware-and-wiring.md) and [board guide](11-g474-board-guide.md).

DTG=149 gives `(64+21)×2/170 MHz=1 µs`. One ADC conversion theoretically takes `(247.5+12.5)/42.5 MHz≈6.12 µs`. Sequential software calls are not synchronous acquisition. A 20 kHz timer does not mean current control is running.

## Regeneration

1. Change `.ioc` and regenerate with CubeMX; do not hand-edit generated regions.
2. Preserve application, emergency shutdown, and SysTick performance hooks in Core USER CODE regions.
3. Keep `src/app`, `src/drv`, `src/bsp`, and `platform` sources in the Keil project.
4. Protect HAL/CMSIS, startup assembly, and compiler options; do not hand-edit SDK files.
5. Rebuild and review errors/warnings. Remeasure hardware timing after timing changes.

Bridge enable and boot self-test are disabled. The temporary waveform demo has been tested and disabled. Synchronized ADC triggering, current-control interrupts, and fast protection remain pending.
