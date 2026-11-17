# 09 Validation and maintenance

**English** | [简体中文](../zh-CN/09-validation-and-maintenance.md) | [Documentation index](../README.md)

The sole target is STM32G474RET6 / DevEBox Ver.20, running bridge-off diagnostics. On 2026-11-17 the user reported a successful temporary boot PWM test and **990 ns dead time**. The demo is disabled; voltage, ADC offset, and control timing still need validation.

## Software checks

Run at the root in Git Bash; adjust the checkout path if necessary:

```bash
"C:/Keil_v5/UV4/UV4.exe" -r "E:/Code/STM32-FOC/MDK-ARM/G474-FOC.uvprojx" -j0 -o "E:/Code/STM32-FOC/build-g474.log"
python -X utf8 tests/run_native.py
```

Keil uses ARM Compiler 5.06 update 7 and STM32G4xx_DFP 1.5.0. Host tests find existing MSVC with `vswhere`; no SDK installation/modification.

| Suite | Coverage |
|---|---|
| `test_runtime` | PWM interlocks, Hall mapping, UART queue, real application startup/stop, ADC failure lockout |
| `test_g474` | Actual platform ADC complete samples, ADC4 VBUS, disabled bridge/boot-test policy |
| `test_perf` | Work-time union, nested interrupts, DWT wrap, deadline budget, reporting |

Runtime substitutes can allow drive to exercise six-step/fault paths. Real G474 policy remains 0, checked by `test_g474`. Register substitutes cannot verify physical complementary output, dead time, or analog accuracy.

## Hardware order

1. Check empty J3, PB10/PB11, SWD, grounds, STM32G474RET6 download target.
2. Keep bridge off; expect MONITOR ONLY, rejected `1`/`2`, PC9 low.
3. Hand-turn to inspect Hall. Measure analog inputs/divider before connecting and compare raw/scaled values.
4. Boot demo is disabled. Explicitly enable a bridge-off test before future rechecks; start capture then reset. Inspect PA8/PB13, PA9/PB14, PA10/PB15 for 20 kHz and 1 µs both-low gaps.
5. Implement synchronous acquisition, current control, and fast 8 A protection before validating same-cycle completeness, worst-case time, latching, and loaded operation.

## Cleanup checkpoint: 2026-11-16

- G474 rebuild: 0 errors/warnings; Code=28140, RO=1492, RW=104, ZI=4496 bytes.
- Three host suites passed, using G474 ADCs, Hall PC6/7/8, SD PC9, ARR=4250, DTG=149.
- At that checkpoint, 104 Core/Drivers/CubeMX/Keil files matched pre-cleanup SHA-256 hashes; cleanup did not change drive policy.
- One project, resolvable source paths/Markdown links; no hardware flashing or measurements during that cleanup checkpoint.

The later user-reported PWM measurement does not establish ADC accuracy or loaded protection.

## 2026-11-17 post-restructure flash validation

- Rebuild: 0 errors/warnings; Code=30904, RO=1488, RW=104, ZI=4608 bytes.
- J-Link download: Erase/Program/Verify OK, target 3.283 V; the application started automatically.
- 115200 8N1 over the J-Link CDC UART: boot log `[BOARD] G474RET6 DevEBox / MONITOR ONLY`, `STM32-FOC v0.1 boot`, `HALL=INV`; `?` help works; `1` returns `[LOCK] Board wiring not verified, monitor only` and the bridge stays off.
- DMA log transmission and command reception work; no truncated long lines observed.
- Bus voltage reads about 22.2–22.4 V (raw 1099–1113); the scaling path is alive.
- Phase-current raw values are about 362/445/985, displayed as -7.9/-7.4/-3.8 A, far from the expected 1.25 V (raw≈1551) zero-current offset. The current-sense chain is not at its expected zero; **do not treat these as valid currents** and do not enable the bridge before checking wiring and offset.
- Hall reads `INV` (000/111), consistent with no motor/Hall wiring attached.
- Performance accounting works while J-Link is attached (CPU work≈55.8%, irq≈0.53 ms/s); after J-Link disconnects it permanently reports `[PERF] unavailable`, i.e. DWT counting depends on the debug domain on this board. Follow-up: check DBGMCU/debug clock or use a timer timebase for standalone statistics.

Not performed: loaded bridge operation, PWM waveform recheck, synchronous current sampling, or current limiting. These results do not replace power-stage acceptance.
