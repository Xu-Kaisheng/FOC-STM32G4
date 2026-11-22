# 09 Operation, build and validation

**English** | [简体中文](../zh-CN/09-validation-and-maintenance.md) | [Documentation index](../README.md)

This page owns current capabilities, drive permission, commands and operating parameters. Wiring belongs to [01](01-hardware-and-wiring.md), module contracts to [06](06-driver-modules.md).

## Capabilities and limits

- Boot is STOP: CTRL_SD low, boot PWM self-test disabled. Explicit `1`/`2` commands start open-loop six-step operation only after ADC/PWM interlocks pass.
- Commutation follows commanded electrical frequency. Hall input is monitoring only, not rotor-position feedback. No automatic alignment, acceleration ramp, current loop or speed loop; smooth low-speed tracking is not guaranteed.
- Current acquisition is sequential software triggering; bus/VREF acquisition is separate. Status reports every 500 ms. Reference compensation: [10](10-bus-voltage-adc.md); performance accounting: [12](12-performance-monitoring.md).
- The maintainer deferred the 8 A software limiter. No over/undervoltage, stall or loss-of-synchronism protection. Slow current reports and duty clamps are not current limiting.
- ADC initialization/acquisition/freshness failures disable power and latch startup lockout until reset. PWM rejection returns STOP. Mode transitions stop power/PWM and wait 200 ms.

## UART commands

UART setup/wiring: [01](01-hardware-and-wiring.md). Commands are single characters; no newline is required.

| Command | Behavior |
|---|---|
| `0` | Stop and disable bridge |
| `1` / `2` | Forward / reverse six-step request; determine shaft direction by observation |
| `h` | Bridge-off Hall transition monitoring |
| `+` / `-` | Increase/decrease duty by 5 percentage points |
| `>` / `<` | Multiply/divide electrical frequency by 1.5 |
| `?` | Help |

| Parameter | Value | Source |
|---|---|---|
| Initial electrical frequency / range | 1 Hz / 0.5–20 Hz | `src/app/control/app_control.c` |
| Initial duty / range | 20% / 5%–60% | Same source |
| Bridge-off transition wait | 200 ms | `src/app/app/app.c` |
| Command increments | 5 percentage points / frequency ratio 1.5 | `src/app/cmd/app_cmd.c` |

`1`/`2` preserve frequency/duty; reset restores defaults. Six steps form one electrical period, with approximately `1000/(6×f)` ms per step, subject to integer rounding and main-loop jitter. With two pole pairs, synchronous tracking gives `rpm=60×f/2`, or about 30 rpm by default. Logged f is a command, not measured speed. Half-second Hall snapshots skip transitions and cannot alone establish reversal.

## Build and download

The only project is `MDK-ARM/G474-FOC.uvprojx`; `STM32-FOC.uvmpw` is its workspace entry.

1. Locate `UV4.exe` on PATH (`Get-Command UV4.exe` on Windows). Otherwise inspect Path under `HKLM\SOFTWARE\Keil\Products\MDK` or its 32-bit view `HKLM\SOFTWARE\WOW6432Node\Keil\Products\MDK`, locate UV4 and verify the executable exists.
2. Set `KEIL_UV4` only for the current terminal. Read toolchain/device-pack requirements from `.ioc`/`.uvprojx`. Do not change compiler options for a personal installation or commit local paths/environment inventories.
3. Run from the repository root in Git Bash on Windows:

```bash
: "${KEIL_UV4:=$(command -v UV4.exe || true)}"
: "${KEIL_UV4:?Locate UV4.exe and set KEIL_UV4 for this shell first}"
"$KEIL_UV4" -r "MDK-ARM/G474-FOC.uvprojx" -j0 -o "../build-g474.log"
python -X utf8 tests/run_native.py
bash tests/check_structure.sh
```

Keil log paths are relative to the project directory; `../` selects the repository root. Check the new log for `0 Error(s)` and explain warnings. Outputs are `MDK-ARM/G474-FOC/G474-FOC.axf|.hex`; outputs/logs are ignored by Git.

Before download verify MCU, probe and configuration, with the power stage in a controlled stopped state. With user authorization, use the existing Keil download setup:

```bash
"$KEIL_UV4" -f "MDK-ARM/G474-FOC.uvprojx" -j0 -o "../build-flash.log"
```

Check `Verify OK`, application startup and UART. Compilation does not establish successful programming, and programming does not validate rotation. Release the UART port after capture.

## Regression and hardware checks

| Test | Coverage |
|---|---|
| test_runtime | PWM/COM interlocks, no automatic boot output, startup/stop, UART, ADC fault latch |
| test_g474 | Actual board policy, ADC conversion/completeness, VREFINT |
| test_perf | Work union, nested interrupts, wrap and DWT recovery |

Host models cannot validate dead time, electrical polarity or real analog sampling. Use the changed module’s hardware method: [PWM/current/Hall/UART](06-driver-modules.md), [bus/VREF](10-bus-voltage-adc.md), [performance](12-performance-monitoring.md). Keep raw logs and experiments in local build output or the conversation, not rolling manual entries.

For the first unloaded test, secure the motor, use a hardware-current-limited supply and prepare power disconnect. Start in STOP; check legal Hall states, zero-current inputs and bus voltage. Send `-` individually until 5%, then briefly try `1` and stop with `0`. Wait for a complete stop before `2`. Failure to start at low duty does not prove wrong phase order; do not keep increasing duty against a stalled rotor. Stop on abnormal vibration/noise or supply limiting. Record continuous direction, Hall transitions and commutation timing before changing control. Do not claim smooth tracking or load validation without evidence.
