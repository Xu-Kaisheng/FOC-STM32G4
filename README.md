# FOC-STM32G4

**English** | [简体中文](README.zh-CN.md)

A bare-metal Hall-sensor FOC project for **STM32G474RET6 (DevEBox Ver.20) + ATK-PD6010B + BL57H95**, without MCSDK. G474 is the only target. The current firmware provides diagnostics with the power bridge disabled; current control, speed control, and fast 8 A software current limiting are not implemented yet.

Open [STM32-FOC.uvmpw](STM32-FOC.uvmpw), or [G474-FOC.uvprojx](MDK-ARM/G474-FOC.uvprojx) directly. The workspace contains one target.

See the [documentation index](docs/README.md) for English and Chinese guides. `datasheet/` contains local references and is not distributed with this repository. Vendor SDK files retain their own licenses.

## Repository layout

```text
STM32-FOC/
├── STM32-FOC.uvmpw           Single-target Keil workspace
├── G474-FOC.ioc              CubeMX configuration source
├── Core/                    Generated initialization and interrupt handlers
├── Drivers/                 Vendor HAL/CMSIS; read-only
├── MDK-ARM/                 Keil project, startup code, build output
├── src/
│   ├── app/                 Application: app/, cmd/, control/, log/, perf/, status/
│   ├── algorithm/           Pure algorithms: sixstep/, foc/ (standard C only)
│   ├── third_party/          External libraries (reserved)
│   └── bsp/                 Board layer: board/, pwm/, hall/, adc_cur/, adc_vbus/, uart/, time/
├── tests/                   Host regression tests, HAL substitutes, structure checker
├── docs/en/                 English documentation
├── docs/zh-CN/              Chinese documentation
└── datasheet/               Local device references; excluded from Git/build
```

Architecture is strictly two-layer: `app` may call `bsp`; `bsp` must never include `src/app/`. Each module folder keeps its `.h`/`.c` pair, and `bsp/` modules must not include each other (only `bsp/board/board.h`). The full plan is in [doc 13](docs/en/13-repo-restructure.md); `bash tests/check_structure.sh` enforces it.

Edit generated code only inside USER CODE regions. Change peripheral configuration through the root `.ioc` and CubeMX. ADC implementations live in `src/bsp/adc_cur/` and `src/bsp/adc_vbus/`.

`tests/` contains regression source for interlocks, ADC handling, and performance accounting. Running it creates disposable output in `build/native-tests/`. Download caches, scratch files, and driver installers do not belong in the source tree.

## Current functionality

- USART3 commands and DMA-backed nonblocking log transmission: `0` stops, `h` monitors Hall inputs, `?` prints help.
- Raw Hall codes and a relative sector lookup table; rotor angle calibration is pending.
- Sequential ADC1/2/3 phase-current reads and independent ADC4 bus-voltage reads every 500 ms. Acquisition failure disables the bridge and latches startup lockout.
- TIM1 has six complementary outputs and 1 µs hardware dead time. **Boot PWM self-test and bridge enable are disabled**; commands `1`/`2` are rejected. On 2026-11-17 the user reported a successful temporary PWM test and **990 ns measured dead time**. The demo is now disabled; hardware dead time remains configured.
- Once-per-second effective CPU work, work/poll/interrupt timing, and control-task average/maximum timing. The current-loop API also counts deadline overruns. `CURRENT=OFF` is expected today; see [performance monitoring](docs/en/12-performance-monitoring.md).

## Key parameters

| Item | Configuration / target |
|---|---|
| MCU | STM32G474RET6, LQFP64, Cortex-M4F, 512 KB Flash, 128 KB SRAM |
| Clock | HSI 16 MHz / 4 × 85 / 2 = 170 MHz |
| PWM | Center-aligned 20 kHz; PSC=0, ARR=4250, RCR=1 |
| Dead time | DTG=149, clock divider 1, 1 µs |
| Phase current | ADC1/2/3 independent regular groups, sequential software triggers; not synchronized |
| VBUS | ADC4_IN3, PB12; `raw × 3.3 × 25 / 4096`; monitoring only |
| ADC | 42.5 MHz kernel, 12-bit, 247.5 sampling cycles, 1 ms polling timeout argument |
| Duty limits | PWM API 5%–92%; six-step tuning 5%–60%, drive currently locked |
| Control targets | Current loop 20 kHz, speed loop 1 kHz; neither implemented; 8500 CPU cycles per 50 µs |
| Protection | 8 A software limit pending; driver-board hardware latch at 10.4 A |
| Motor | 24 V, 4 poles (2 pole pairs), 3000 rpm, 6.6 A, 0.33 N·m |

## Wiring

| CN3 | Signal | G474 pin | Function |
|---:|---|---|---|
| 2 / 4 / 6 | HALL U / V / W | PC6 / PC7 / PC8 | GPIO pull-ups; main-loop polling |
| 8 | VBUS | PB12 | ADC4_IN3, ×25 |
| 10 / 12 / 14 | AMP_IU / IV / IW | PC0 / PC1 / PB1 | ADC1_IN6 / ADC2_IN7 / ADC3_IN1 |
| 17 / 18 | PWM_UH / UL | PA8 / PB13 | TIM1_CH1 / CH1N |
| 19 / 20 | PWM_VH / VL | PA9 / PB14 | TIM1_CH2 / CH2N |
| 21 / 22 | PWM_WH / WL | PA10 / PB15 | TIM1_CH3 / CH3N |
| 24 | CTRL_SD | PC9 | Active high; initialized low; enable prohibited |
| 7 / 16 | GND | GND | Connect both grounds |
| 15 | VTEMP | Unconfigured | Leave disconnected |
| 23 | 5V_IN | **Do not connect** | Prevent backfeeding |
| — | Debug TX / RX | PB10 / PB11 | USART3, 115200 8N1, 3.3 V logic |

The board schematic and DS12288 Rev 6 support this assignment. **Leave OLED/TFT connector J3 empty**: it shares PB12–PB15 and PC6/PC7. Flash uses PA15/PC10/PC11/PC12, LEDs use PC13/PD2, and buttons use PA0/PA1; none conflict. Reserve USB PA11/PA12, SWD PA13/PA14, and oscillator pins PF0/PF1 and PC14/PC15.

See the [G474 board guide](docs/en/11-g474-board-guide.md) for J4/J5 pin numbers, rear-view locations, power, and logic-analyzer wiring. Driver-board and motor wiring are in [hardware and wiring](docs/en/01-hardware-and-wiring.md).

## Build and validation

Environment: CubeMX 6.16.1, STM32CubeG4 v1.6.3, STM32G4xx_DFP 1.5.0, ARM Compiler 5.06 update 7. Run in Git Bash on Windows, adjusting the checkout path if necessary:

```bash
"C:/Keil_v5/UV4/UV4.exe" -r "E:/Code/STM32-FOC/MDK-ARM/G474-FOC.uvprojx" -j0 -o "E:/Code/STM32-FOC/build-g474.log"
python -X utf8 tests/run_native.py
```

Output: `MDK-ARM/G474-FOC/G474-FOC.axf|.hex`. Check for `0 Error(s)` and review warnings. The user has tested the temporary PWM demo and reported 990 ns dead time; the demo is now disabled. Select download tooling and connections for the actual probe. See [validation and maintenance](docs/en/09-validation-and-maintenance.md) for coverage and remaining hardware work.
