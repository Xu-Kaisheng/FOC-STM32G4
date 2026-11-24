# 09 Operation, build and validation

**English** | [简体中文](../zh-CN/09-validation-and-maintenance.md) | [Documentation index](../README.md)

This page owns current capabilities, drive permission, commands and operating parameters. Wiring belongs to [01](01-hardware-and-wiring.md), module contracts to [06](06-driver-modules.md).

## Capabilities and limits

- Boot enters STOP with CTRL_SD low. Command `1` starts fixed-electrical-angle Id/Iq control, first calibrating zero offsets with the bridge disabled, then enabling power. Synchronized acquisition and current control run at 20 kHz.
- Hall provides monitoring while rotor-angle offset and direction await calibration. This stage supports stationary or controlled bench current commissioning. Speed control and continuous-rotation control remain pending.
- Any phase reaching ±8 A, raw saturation, missing frame, PWM interlock failure or missed duty window immediately disables power and latches a fault. A phase-current sum exceeding 1 A for 3 consecutive frames also latches shutdown, as do bus range, freshness and VREF drift failures. Reset is required after faults. Command `0` stops operation.
- An independent TIM1 update IRQ detects missing ADC frames. The main loop updates the bus and the current ISR checks freshness. Hardware overcurrent protection remains necessary. Verify analog saturation, polarity, worst-case ISR duration and control stability on hardware.

## UART commands

UART setup/wiring: [01](01-hardware-and-wiring.md). Commands are single characters; no newline is required.

| Command | Action |
|---|---|
| `0` | Revoke permission, disable bridge, stop PWM and ADC |
| `1` | Start synchronized sampling, then fixed-angle current control after zero calibration |
| `h` | Disable power and monitor Hall changes |
| `+` / `-` | Increase / decrease Iq reference by 0.1 A |
| `]` / `[` | Increase / decrease Id reference by 0.1 A |
| `>` / `<` | Change fixed electrical angle by ±15° while stopped, retaining the angle during operation |
| `p` | Print one task and interrupt utilization report for the latest one-second window |
| `v` | Start the 50 Hz VOFA+ stream, or stop it if already running |
| `?` | Help |

| Parameter | Setting | Source |
|---|---|---|
| Default Id / Iq and per-axis range | 0 A, ±1 A | `src/app/control/app_current.c` |
| Reference ramp | 0.0001 A per cycle, 2 A/s | Same |
| Default fixed angle | 0°, wrapped at 360° | Same |
| Initial PI gains | Kp=0.5 V/A, Ki×Ts=0.005 V/A | Same |
| Per-axis voltage limit | ±1.4 V | Same |
| Bus range / refresh / maximum age | 8–30 V / 10 ms / 20 ms | `src/app/control/app_control.c` |
| Allowed VREF drift | ±50 mV relative to startup | Same |
| Bridge-off wait | 200 ms | `src/app/app/app.c` |

References and angle survive normal stop/start cycles. Each restart ramps from zero current. PI gains are commissioning defaults because phase resistance and inductance remain unmeasured. Limits apply independently to both axes. The commissioning range is separate from motor ratings.

Fault codes follow `app_fault_t`. Code 1 covers ADC and zero calibration errors, 2 overcurrent, 3 PWM/modulation/computation deadlines, 4 missing frames, 5 bus/VREF faults, and 6 persistent current-sum errors. Calibration restarts its stable window after isolated spikes and is limited to 102.4 ms, with the bridge off throughout.

After the boot messages, the UART is quiet by default. Startup events, faults and command replies still produce messages. Send `p` to display the `[CPU]` table for the most recently completed window. The first window takes one second to become available. See [performance monitoring](12-performance-monitoring.md) for the fields.

In VOFA+, select **JustFloat** and **115200, 8N1**, then send lowercase `v` as text. The stream uses little-endian floats with a four-byte terminator. [JustFloat protocol](https://www.vofa.plus/docs/learning/dataengines/justfloat/)

Each 20 ms frame contains these zero-based channels.

| Channel | Field | Meaning |
|---|---|---|
| 0 / 1 | `Iq` / `Id` | Measured q / d current from the same control cycle, A |
| 2 | `Iq_ref` | Ramped q-axis reference used in that cycle, A |
| 3 | `Theta_elec` | Fixed electrical angle used by the current loop, degrees in [0, 360) |
| 4 / 5 | `Speed_ref` / `Speed_fdb` | Reserved mechanical speed target / feedback in rpm. Both transmit NaN until the speed loop is integrated. |

The three current channels transmit NaN during STOP and calibration. While streaming, `+` and `-` still adjust current with text echoes suppressed. Commands `p`, `?` and `h` end the stream before producing their text. Faults and rejected starts also return to text output. If the main loop stalls or UART is busy, the stream skips missed frames and resumes with the latest sample.

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

Host regression covers coordinate transforms, independent three-phase RL tracking, anti-windup, duty bounds, zero calibration, complete-frame publication, fault latching, serial start/stop, and existing logging/performance behavior. Validate physical sampling windows, dead time, polarity and load capability on hardware.

For initial commissioning, secure the motor and set a hardware supply current limit. With the bridge disabled, check all six complementary outputs, trigger phase and analog settling. Confirm zero Id/Iq references, send `1`, and verify calibration and stationary zero current. Then adjust one axis at a time by 0.1 A and check measured-current convergence. A fixed angle can align the rotor or produce holding torque. Stop with `0` and disconnect motor power if vibration, increasing wrong-sign current or supply limiting occurs.

The `CURRENT` row shows average and maximum current-control duration and the count of executions taking at least 50 µs; this time is already included in ADC3 utilization. Measure the complete ADC ISR's worst-case duration with a scope, targeting below 15 µs, and verify duty application at the next underflow. Confirm missing-frame and overcurrent faults immediately lower CTRL_SD and reject subsequent start commands. Calibrate Hall offset and interpolation after current-loop validation. Successful builds do not establish hardware acceptance.
