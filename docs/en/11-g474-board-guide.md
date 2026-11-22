# 11 G474 board and wiring verification

**English** | [简体中文](../zh-CN/11-g474-board-guide.md) | [Documentation index](../README.md)

This page owns physical headers, shared nets and power references for DevEBox Ver.20. CN2/CN3 wiring belongs to [01](01-hardware-and-wiring.md); operation/build belong to [09](09-validation-and-maintenance.md).

## What the photographs establish

The user identified STM32G474RET6. Photos show DevEBox branding, two 15×2 headers, SWD, native Type-C USB, W25Q16 Flash, OLED/TFT, LEDs/buttons, and 8 MHz / 32.768 kHz crystals. The rear photo reads `DevEBox / STM32G474R / Ver.20`. Schematic and DS12288 Rev 6 checks were added on 2026-11-16.

A photo proves header labels/locations, not absence of onboard loads. The following is the **rear view with USB at the top**. Count rows downward from power pins. Outer means board edge; inner means board center. The component-side view is mirrored; do not reuse left/right positions after flipping the board.

| Row | Left outer | Left inner | Right inner | Right outer |
|---:|---|---|---|---|
| 1 | 3V3 | 3V3 | 5V | 5V |
| 2 | VREF+ | VDDA | GND | GND |
| 3 | GND | GND | PB12 | RST |
| 4 | PB11 | PB10 | PB13 | PB14 |
| 5 | PB1 | PB2 | PB15 | PC6 |
| 6 | PC5 | PB0 | PC7 | PC8 |
| 7 | PA7 | PC4 | PC9 | PA8 |
| 8 | PA5 | PA6 | PA9 | PA10 |
| 9 | PA3 | PA4 | PA11 | PA12 |
| 10 | PA1 | PA2 | PA15 | PC10 |
| 11 | PC3 | PA0 | PC11 | PC12 |
| 12 | PC1 | PC2 | PD2 | PB3 |
| 13 | PC15 | PC0 | PB4 | PB5 |
| 14 | PC13 | PC14 | PB6 | PB7 |
| 15 | E_BAT | GND | PB8 | PB9 |

VDDA/VREF+/E_BAT are supply/reference/backup pins, not GPIO. R11 (0 Ω) connects 3V3 to VDDA; R10 (0 Ω) connects VDDA to VREF+. EX_BAT and 3V3 feed VBAT through Q1 (BAT54C); retain the board arrangement.

Rear-view SWD labels from left to right are RST/SWCLK/SWDIO/GND/3V3. PB8 is marked BOOT0 and unused here; do not casually change boot pulls. PA9/PA10 TX1/RX1 labels describe USART1 alternate capability; firmware uses TIM1 there and USART3 on PB10/PB11.

PA13/PA14 serve SWD, PA11/PA12 native USB, PC14/PC15 LSE; reserve them. Onboard occupancy is checked below. Leave J3 unpopulated.

## Driver interface

Use the single CN2/CN3 wiring table in [01](01-hardware-and-wiring.md). This page maps MCU pins to physical board headers without copying the CN3 assignment.

## Onboard nets checked on 2026-11-16

Source: page 1 of `STM32G474R开发板-原理图--202508.pdf`. Page 2 repeats similar circuitry without a QR code; page 3 is layout/dimensions. Schematic title is SN:202408, physical board Ver.20. Assembly differences must be checked on the actual board.

| Device/interface | Nets | Effect |
|---|---|---|
| W25Q16 U3 | PA15 CS, PC10 SCK, PC11 MISO, PC12 MOSI | No conflict; reserve for Flash |
| LEDs D1/D2 | PC13/PD2, each via 510 Ω, active low | No conflict; power LED uses no GPIO |
| K1/K2 | PA0/PA1, pressed to ground | No conflict |
| OLED/TFT J3 | 3:PB15, 4:PB13, 5:PB12, 6:PB14, 7:PC6, 8:PC7 | Shares low-side PWM, VBUS, two Hall signals; **leave empty** |
| USB Type-C | PA11/PA12 via R4/R5 (0 Ω) to D−/D+ | Reserve; no USB–UART IC |
| SWD J2 | 1:3V3, 2:GND, 3:PA13, 4:PA14, 5:NRST | Debugger |
| Crystals | PF0/PF1 8 MHz; PC14/PC15 32.768 kHz | Reserve; current clock remains HSI+PLL |
| BOOT0 | PB8, R6 5.1 kΩ pull-down | Unused by this application |

J3 nets connect directly to MCU/header, with no additional buffer or pull shown. No additional onboard loads are shown on PC0/PC1/PB1, PC8/PC9, PA8/PA9/PA10, or PB10/PB11.

Connector numbers below remove front/back ambiguity: **J4-13 means pin 13, not row 13**.

| Signal | MCU | Schematic header |
|---|---|---|
| UH/VH/WH | PA8/PA9/PA10 | J4-13 / J4-16 / J4-15 |
| UL/VL/WL | PB13/PB14/PB15 | J4-8 / J4-7 / J4-10 |
| CTRL_SD | PC9 | J4-14 |
| HALL U/V/W | PC6/PC7/PC8 | J4-9 / J4-12 / J4-11 |
| AMP_IU/IV/IW | PC0/PC1/PB1 | J5-25 / J5-24 / J5-10 |
| VBUS | PB12 | J4-6 |
| USART3 TX/RX | PB10/PB11 | J5-7 / J5-8 |

DS12288 Rev 6 Table 12 (pp. 59,61,63) lists PC0=ADC12_IN6, PC1=ADC12_IN7, PB1=ADC3_IN1/ADC1_IN12, PB12=ADC4_IN3/ADC1_IN11. Table 13 (pp. 73–75) confirms TIM1_CH1/2/3 AF6 on PA8/9/10, CH1N/2N AF6 on PB13/14, **CH3N AF4 on PB15**, and USART3 AF7 on PB10/11. Hall inputs need GPIO, not timer alternate functions.

**Development-board VBUS is USB 5 V; driver CN3-8 VBUS is the motor-bus divider output to PB12/J4-6.** Do not join these nets or connect raw motor-bus voltage to an ADC.

## Debug power and ADC reference

The user confirmed their J-Link 3V terminal is a power output. For standalone board power, connect it to 3V3 (J2-1 or J5-1/2), ground to GND, SWDIO to J2-3, SWCLK to J2-4, optionally NRST to J2-5. Check actual output voltage/current and disconnect Type-C/external 5 V. A conventional VTref sensing input is not a supply output.

3V3 is directly connected to U2 RT9193-33GB output; no isolation is shown. External 3V3 also biases that regulator output. Reverse-feed tolerance has not been confirmed from its datasheet, so the schematic does not establish every supply combination as safe. Board power must not supply the motor power stage.

R11/R10 connect the rail to VDDA/VREF+. If actual supply is 3.0 V while scaling assumes 3.3 V, inferred ADC input voltage is about 10% high, and current-offset subtraction is affected. ADC4 now measures VREFINT for compensation; compare its VREF+ result with a meter. Analog range follows actual VDDA/VREF+, not digital-pin voltage-tolerance rules.

## References

Board schematics, DS12288, RM0440 and the driver-board manual are local read-only `datasheet/` references. Firmware configuration lives in `.ioc` and source; see [generated configuration](04-cubemx-configuration.md). Drawing checks do not establish identical assembly on every board; verify labels and orientation before wiring.
