# 11 G474 board and wiring verification

**English** | [简体中文](../zh-CN/11-g474-board-guide.md) | [Documentation index](../README.md)

## Current scope

Only STM32G474RET6 / DevEBox Ver.20 is supported. `STM32-FOC.uvmpw` opens `MDK-ARM/G474-FOC.uvprojx`; the configuration source is root `G474-FOC.ioc`.

The project builds and supports UART, Hall, phase-current, and bus-voltage diagnostics. This does not establish completed motor control. Current ADCs are **sequentially software triggered**, not sampled by a common PWM event. There is no current loop. Bridge-enable and boot-self-test policies both return 0. The temporary boot demo has been tested and disabled.

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

## Driver-board wiring

Selected pins were matched against the rear photo, CubeMX LQFP64 database, generated code, and DS12288 Rev 6. **The assignment is usable if J3 stays empty and display functions are not enabled concurrently.** Schematic checks do not establish physical continuity/voltage; the later PWM measurement is recorded separately below.

| CN3 | Signal | MCU | Peripheral | Rear-view position |
|---:|---|---|---|---|
| 17 | PWM_UH | PA8 | TIM1_CH1 | Right 7 outer |
| 18 | PWM_UL | PB13 | TIM1_CH1N | Right 4 inner |
| 19 | PWM_VH | PA9 | TIM1_CH2 | Right 8 inner |
| 20 | PWM_VL | PB14 | TIM1_CH2N | Right 4 outer |
| 21 | PWM_WH | PA10 | TIM1_CH3 | Right 8 outer |
| 22 | PWM_WL | PB15 | TIM1_CH3N | Right 5 inner |
| 24 | CTRL_SD | PC9 | Push-pull, default low | Right 7 inner |
| 2 | HALL U | PC6 | Pull-up input | Right 5 outer |
| 4 | HALL V | PC7 | Pull-up input | Right 6 inner |
| 6 | HALL W | PC8 | Pull-up input | Right 6 outer |
| 10 | AMP_IU | PC0 | ADC1_IN6 | Left 13 inner |
| 12 | AMP_IV | PC1 | ADC2_IN7 | Left 12 outer |
| 14 | AMP_IW | PB1 | ADC3_IN1 | Left 5 outer |
| 8 | VBUS | PB12 | ADC4_IN3 | Right 3 inner |
| 7,16 | GND | GND | Connect both | Confirmed ground pins |
| 15 | VTEMP | Disconnected | Unconfigured | — |
| 23 | 5V_IN | **Do not connect** | Prevent backfeeding | — |

CN3-1/3/5 encoder and 9/11/13 BEMF remain disconnected. Use the signal labels and table, not cable order.

USART3: PB10 (left 4 inner) → USB–TTL RX; PB11 (left 4 outer) → USB–TTL TX; common ground; 115200 8N1, 3.3 V logic. Type-C is native MCU USB, with no USB CDC firmware and no onboard USB–UART bridge.

Four ADCs reserve independent resources for later work, but trigger compatibility, sample time, and valid PWM windows still need implementation/measurement. No zero-skew claim is made.

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

R11/R10 connect the rail to VDDA/VREF+. If actual supply is 3.0 V while scaling assumes 3.3 V, inferred ADC input voltage is about 10% high, and current-offset subtraction is affected. Record measured VREF+ and calibrate later. Analog range follows actual VDDA/VREF+, not digital-pin voltage-tolerance rules.

## Project organization

```text
STM32-FOC.uvmpw              Single Keil entry
G474-FOC.ioc                 CubeMX source
Core/ Drivers/ MDK-ARM/      Generated setup, vendor SDK, Keil
src/app/                    Application: app/, cmd/, control/, log/, perf/, status/
src/algorithm/              Pure algorithms: sixstep/, foc/
src/third_party/            External libraries (reserved)
src/bsp/                    Board layer: board/, pwm/, hall/, adc_cur/, adc_vbus/, uart/, time/
```

The application does not access HAL ADC handles. Regeneration must preserve the `src/app` and `src/bsp` entries and the USER CODE application, emergency-stop, and performance hooks. Layout and dependency rules: [13-repo-restructure.md](13-repo-restructure.md).

## Configuration and measured PWM test

The temporary 2026-11-17 firmware produced 10 seconds of three in-phase complementary PWM pairs at 20 kHz with CCR=50% ARR and nominal 1 µs dead time. PC9 stayed low by design. Completion stopped counter/MOE/channel enables, cleared CCRs, and entered diagnostics. The user reported normal operation and **990 ns dead time**. Boot test is now disabled and the temporary 10-second duration reverted; the original dormant self-test entry and hardware dead time remain. No capture file, sample rate, or per-channel measurement set was supplied, so this does not certify loaded power-stage operation.

To repeat later, explicitly enable a bridge-off self-test. Connect PA8/PB13, PA9/PB14, PA10/PB15 and GND; optionally capture PC9; leave J3 empty. Start capture before reset, preferably ≥20 MS/s, trigger on PA8 rising. Expect ~50 µs periods, ~1 µs both-low switching gaps, and no both-high overlap. Dead time reduces ideal 25 µs high pulses to roughly 24 µs (~48% actual duty). The three test phases have no 120° offset. A lower-rate long capture can separately verify timed stop.

| Item | Configuration |
|---|---|
| Clock | HSI 16 MHz / 4 ×85 /2 = 170 MHz; no HSE dependency |
| TIM1 | Center-aligned mode 1; PSC=0, ARR=4250, RCR=1, 20 kHz |
| Dead time | Divider 1, DTG=149; `(64+21)×2/170 MHz=1 µs` |
| Output policy | AOE off; boot self-test off; bridge enable prohibited |
| Duty | Common 5%–92%; six-step 5%–60%, drive locked |
| ADC1–4 | Independent, 12-bit right-aligned, one regular software-triggered channel each, 247.5 sampling cycles |
| ADC kernel | 42.5 MHz; ~6.12 µs theoretical single conversion, excluding software |
| Monitoring | 500 ms; sequential phase reads and independent ADC4 |
| Temperature/VREFINT | Not implemented; fixed 3.3 V scaling |
| Control/protection | Common trigger, same-cycle aggregation, current loop, 8 A limit pending |

`app_board_drive_allowed()` and `app_board_boot_test()` return 0. Application command handling and the PWM driver independently reject power enable. Do not remove this policy based on compilation alone.

## Development and verification order

1. With power removed, check physical labels/header orientation/J3 against J4/J5 numbers; use continuity checks for assembly differences.
2. Connect debugger/UART first. Expect `G474RET6 DevEBox / MONITOR ONLY`, rejected `1`/`2`, PC9 low, no automatic PWM.
3. With bridge disabled, verify Hall output levels, hand-turn codes, analog ground voltages; expect ~0.96 V divider at 24 V bus and ~1.25 V at zero current.
4. Validate complementary polarity/dead time as required, then implement common ADC triggering, completeness, and fast protection.
5. Current 2 Hz sequential reads are wiring diagnostics, not loaded current limiting or proof of simultaneous samples. Follow the [roadmap](03-optimization-roadmap.md).

The 2026-11-16 checkpoint covered drawings and documentation only. The later user-reported PWM check is recorded above.

## Performance monitoring

DWT reports effective work ratio/time, polling time, and instrumented interrupts every second. Six-step/current calls are absent under the locked policy, so `SIXSTEP=OFF` and `CURRENT=OFF` are expected. See [performance monitoring](12-performance-monitoring.md).

## References and build

Local references, not distributed in this repository:

- Product photo: `datasheet/STM32G474RET6/Snipaste_2026-11-15_15-44-29.png`.
- Rear photo supplied by the user; original JPG was not found in the local reference folder at documentation time. Header locations were cross-checked with the schematic.
- Board schematic: `datasheet/STM32G474RET6/STM32G474R开发板-原理图--202508.pdf`, three pages.
- MCU datasheet: `STM32G474R数据手册.pdf`, DS12288 Rev 6, 236 pages, in the same folder.
- ADC guidance: `如何在STM32微控制器中获得最佳ADC精度.pdf`, AN2834.
- CubeMX device description `STM32G474R(B-C-E)Tx.xml`, cross-checked against generated GPIO alternates/ADC channels; the CubeMX version is recorded in `.ioc` metadata.
- [STM32CubeG4](https://github.com/STMicroelectronics/STM32CubeG4); use `.ioc` and vendored SDK metadata for versions. SDK content is read-only.
- Read Keil compiler/device-pack requirements from `.uvprojx`; agents discover installation locations on the current machine.

Locate Keil and set `KEIL_UV4` in the current shell as described in [validation and maintenance](09-validation-and-maintenance.md), then run from the repository root in Git Bash:

```bash
: "${KEIL_UV4:=$(command -v UV4.exe || true)}"
: "${KEIL_UV4:?Locate UV4.exe and set KEIL_UV4 for this shell first}"
"$KEIL_UV4" -r "MDK-ARM/G474-FOC.uvprojx" -j0 -o "../build-g474.log"
python -X utf8 tests/run_native.py
```

Check `0 Error(s)`; firmware output is in `MDK-ARM/G474-FOC/`.
