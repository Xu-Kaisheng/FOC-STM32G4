# 10 ADC4 bus-voltage monitoring

**English** | [简体中文](../zh-CN/10-bus-voltage-adc.md) | [Documentation index](../README.md)

## Function and wiring

`src/bsp/adc_vbus/adc_vbus.c` and `adc_vbus.h` use ADC4's independent regular group for slow bus-divider monitoring. Connect CN3-8 to PB12 (ADC4_IN3), CN3-7/16 to MCU ground. The 12 kΩ + 12 kΩ + 1 kΩ divider gives:

```text
Vbus = raw × measured_VREF(V) × 25 / 4095
At VREF+=3.3 V: 24 V bus → about 0.96 V at PB12 → raw about 1192
```

The 12-bit full scale is consistently 4095, matching current scaling and ST helpers. Connect the divided output only, never raw bus voltage. Leave CN3-23 disconnected.

## Configuration and API

| Item | Configuration / contract |
|---|---|
| ADC4 | 12-bit, right-aligned, single-ended, regular rank 1=IN3, rank 2=VREFINT; AutoWait enabled |
| Sampling | 247.5 cycles for divider settling |
| Clock | SYSCLK/4=42.5 MHz, ADC345 domain |
| Trigger | One-shot software; no continuous conversion, DMA, or ADC4 interrupt |
| Schedule | One boot read, then every 500 ms in main loop |
| `bsp_adc_vbus_init()` | Internal ADC4 handle; calibrate bridge-off; 1 success / 0 failure; public API has no HAL |
| `bsp_adc_vbus_read(out)` | Average 16 pairs; publish raw, voltage, vref_mv and sampled_ms only after complete success |
| Failure | Output unchanged; caller must not reuse old sample |

The poll timeout argument is 1 ms; HAL stop also has internal timeouts, so this is not a whole-function 1 ms bound. Each channel takes about 6.12 µs theoretically; one read performs 32 conversions, unmeasured on hardware. Never call this driver in an ISR.

## Sequence and fault behavior

1. CubeMX configures ADC1–4 before application startup; PB12 is analog with no pulls.
2. `app_init()` disables bridge, initializes current/VBUS, and tries VBUS once. Any failure latches startup lockout.
3. Every 500 ms, status handling reads VBUS/VREFINT before currents, passes the same reference to current scaling, and rejects a complete sample older than 20 ms and prints a separate line, e.g. `VBUS=24.01 V | raw=1192 | VREF=3300 mV`, respecting the UART 127-byte limit.
4. Start/poll/stop failure or an out-of-range code stops PWM/bridge, enters STOP, and requires reset; no automatic re-enable.

ADC4 does not add channels to the current sequence, change its trigger, or modify shared ADC clocks at runtime.

## Validation

Host tests cover zero/~24 V/full-scale scaling, all acquisition failure stages, no invalid publication, runtime shutdown, and restart rejection. See [validation](09-validation-and-maintenance.md).

Hardware validation method: keep CTRL_SD low, verify wiring/common ground with a current-limited supply; compare bus/PB12 readings (24 V/0.96 V nominal) and 2 Hz UART reports; compare 12 V and 24 V points to a meter, recording raw/scaled error without exceeding board ratings. With power disconnected, disable ADC4 via debugger after startup to inject failure: expect error log, PC9 low, PWM off, and rejected `1`/`2` until reset. Verify PC0/PC1/PB1 current reporting and record task timing; recheck ISR jitter when current control is added.

## Limitations

- Reference temperature drift, amplifier errors and divider tolerance remain; divider gain needs a known external voltage.
- 2 Hz monitoring is subject to main-loop work and 200 ms transition waits, not a protection path.
- No over/undervoltage, open-wire, or saturation diagnosis. Codes 0/4095 do not prove supply health.
- VTEMP and fast 8 A limiting are pending; acquisition-failure shutdown is not voltage/current protection.
- No hardware accuracy, timing, or waveform data has been collected for this ADC feature.

## Reference compensation

Initialization waits 1 ms after VREFINT is enabled. `VREF_mV=3000×factory_calibration/VREFINT_average` estimates VREF+. Zero codes, invalid calibration words and results outside the board diagnostic range of 2700–3600 mV are rejected. There is no fixed-3.3 V fallback, VREFBUF activation, or reference cache reused between cycles.

Keep the bridge disabled and compare logged VREF with a meter at VREF+, then verify phase input voltages and VBUS. Accuracy claims require measurements for the tested conditions.
