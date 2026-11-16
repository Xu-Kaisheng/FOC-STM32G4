# 03 Optimization roadmap

**English** | [简体中文](../zh-CN/03-optimization-roadmap.md) | [Documentation index](../README.md)

Scope: STM32G474RET6 + ATK-PD6010B + BL57H95, Hall FOC, nominal 20 kHz. Establish motion, then stability and quality, then optimize execution. **Roadmap figures are estimates or acceptance targets, not measured improvements.** Current hardware has a schematic-checked bridge-off diagnostic project; synchronized acquisition and closed-loop control remain pending.

## 0. Baseline

| Item | Value | Basis |
|---|---|---|
| CPU | 170 MHz Cortex-M4F + DSP | DS12288 |
| PWM | 20 kHz, 50 µs, **8500 CPU cycles** | Design |
| Full current-loop timing | Not implemented / not measured | Pending |
| Dead time | Nominal 1 µs, 2% of PWM period | Design for optocoupler/IR2110 chain |
| Current input | Three 20 mΩ shunts; 120 mV/A | Board schematic |
| Hardware trip | 10.4 A latch, reset by lowering CTRL_SD | Manual §2.1.5 |
| Motor | 4 poles / 2 pairs, 3000 rpm, 6.6 A | Specification |
| Hall sector | 1.67 ms at 3000 rpm | Calculation |

Record measured baselines before claiming a benefit. The user-reported 990 ns PWM dead time is documented in the [board guide](11-g474-board-guide.md).

## 1. Measurement infrastructure

| Tool | Method | Observation |
|---|---|---|
| Cycle counter | DWT timestamps around ISR | Cycles and min/max spread over at least 10,000 periods |
| Worst-case timing | Compare DWT maximum to GPIO marker | Preemption and Flash access variability |
| Waveforms | Candidate DAC outputs PA4/PA5 for id/iq | Oscilloscope visibility; configure and account for overhead before use |

UART is for slow status, speed, temperature, and faults. Never print from a control ISR. Performance accounting is implemented; DAC instrumentation is a proposal.

## 2. P1: prerequisites for stable current control

**Fast 8 A software limiting is deferred.** Slow current reports do not implement overcurrent detection, fast shutdown, or a software overcurrent latch. Add these after reliable synchronous acquisition and measure response, latching, and manual recovery. A duty cap is not a current limit.

| ID | Work | Method / intended benefit | Cost estimate / validation |
|---|---|---|---|
| 1.1 | Current-offset calibration | Average 256 bridge-off samples per channel | Startup delay; verify near-zero current, measure actual duration |
| 1.2 | VDDA calibration | VREFINT plus factory calibration | Compare voltage/gain against a meter; accuracy depends on full error budget |
| 1.3 | PI anti-windup | Limit integral, add back-calculation | Roughly 10 extra cycles; inspect current step/overshoot and recovery |
| 1.4 | Sampling-window clamp | Keep duty at 5%–92%, preserve low-side settling and bootstrap charging | Check maximum-duty waveforms; the clamp alone does not prove a valid ≥3 µs window |
| 1.5 | Latch recovery | Bridge off ≥200 ms, remove cause, require deliberate restart | Mode-change wait exists; software overcurrent detection does not. Inject faults with power disconnected and measure SD-low duration |
| 1.6 | Startup sequence | Hold SD low during setup, establish/check PWM before permitting power | Scope SD/PWM ordering; real G474 enable remains locked |
| 1.7 | Parallel phase acquisition | Separate ADCs, common PWM event, publish complete triple then execute one loop | Validate timing skew, completeness, and PWM update deadline |

Configuration alone cannot establish stall safety.

### 2.1 Three-ADC current acquisition

The board is available; schematic occupancy has been checked and **J3 must stay empty**. Current ADCs are sequentially software triggered. The following is future work, not a claim of simultaneous conversion.

#### Current state and goal

- ADC1/2/3 each use a single regular channel, started sequentially.
- Intended mapping is PC0/ADC1_IN6, PC1/ADC2_IN7, PB1/ADC3_IN1, with matched clocks/resolution/sample times and a shared PWM hardware event.
- Three ADCs do not inherently guarantee simultaneity. Check injected-trigger matrices, synchronization restrictions, and errata in the G474 reference manual/CubeMX.
- Do not assume a special triple simultaneous multimode exists. Measure cross-ADC skew even with a common event.

#### Board requirements

| Area | Requirement |
|---|---|
| Package/headers | Confirm three accessible inputs on independent ADCs, not three channels of one ADC |
| PWM/control | Three complementary pairs from one advanced timer, Hall, SD, UART, SWD; check onboard conflicts |
| Slow analog | Plan VBUS, VTEMP, VREFINT without occupying current windows |
| Front end | Driver board already amplifies/biases current; check range, reference, source impedance, settling, grounding; do not add MCU op-amps by default |
| Wiring | Verify alternate functions; keep CN3-23 disconnected and preserve phase/Hall correspondence |

#### Implementation order

1. Measure the sequential baseline: trigger, sample instants, total delay, offset, noise. Compare algorithms under identical PWM/load conditions rather than attributing all gains to parallel ADCs.
2. Configure through `.ioc`/CubeMX. Recalculate PWM/dead-time/trigger timing and synchronize board parameters, README, [hardware guide](01-hardware-and-wiring.md), and AGENTS. Protect vendor/generated code.
3. Prefer one injected conversion per ADC with a shared event inside the valid PWM window. Check center alignment, RCR, edge selection, and exactly one event per control period. Three consecutive software starts are not parallel triggering.
4. Calibrate channels separately. Publish only all three results from the same trigger cycle, then execute the loop once. Never mix old/new data or wait for another ADC inside the ISR. Missing/late/overrun data must disable SD and latch a fault.
5. Derive the common low-side window from duties, dead time, optocoupler/driver delays, and analog settling. Parallel acquisition does not make an invalid conduction window usable. Retain 20 kHz, 1 µs dead time, 5%–92% clamp, and the 8 A target while measuring margin.
6. Keep ADC result aggregation in `adc_cur`. Validate sampling/protection before closing control. At 170 MHz/20 kHz the budget is 8500 cycles; target ISR <15 µs (<2550 cycles) and meet the actual preload-update deadline.

#### Acceptance records

| Check | Method / requirement |
|---|---|
| Calibration | Measure bridge-off zero and known-voltage gain; record VDDA/VREF, sample time, temperature |
| Parallel timing | With power disabled and driver analog outputs disconnected, feed the same compliant dynamic source to ADC inputs; measure amplitude/skew. Never short driver outputs together. Derive skew tolerance from allowed current error and maximum di/dt, not ISR arrival times |
| Same-cycle completeness | At least 10,000 cycles: one complete publication each, no stale/missing data, overrun, or duplicate loop. Inject missing data and confirm shutdown/latch |
| Window/deadline | Align PWM, trigger marker, ISR marker; record conversion delay, worst ISR time, jitter, update margin across sectors/duty limits |
| Current consistency | Evaluate IU+IV+IW only when all windows and calibrations are valid; compare against baseline using an error-budget threshold |
| Protection | Verify SD low at boot, dead time, 8 A action, latch, ≥200 ms deliberate recovery. Do not mark current protection passed before implementation |

Board identification and diagnostic mapping are checked; synchronized timing and closed-loop acceptance are not complete.

## 3. P2: motion quality

| ID | Work | Method | Intended result / validation |
|---|---|---|---|
| 2.1 | Dead-time compensation | Adjust duty by current sign and `Tdead/Tpwm` | Lower low-speed distortion; inspect phase current at 50 rpm; ~40 cycles estimate |
| 2.2 | Angle advance | Candidate `θ+=ω·1.5·Ts`, after measuring actual delay | Better high-speed decoupling; inspect id at 3000 rpm; ~10 cycles |
| 2.3 | Hall interpolation | `θ=θ0+ω·Δt`, correct at edges | Reduce 60° steps; inspect 6th-order iq ripple; ~20 cycles |
| 2.4 | Current PI tuning | `Kp=L·ωc`, `Ki=R·ωc·Ts` | Candidate 1 kHz bandwidth; measure R/L and step response |
| 2.5 | Speed filtering | Sector timing plus first-order low-pass or FMAC IIR | Compare 100 rpm noise/speed ripple; ~30 cycles |
| 2.6 | Three-stage start | Candidate alignment 2 A/500 ms → open-loop acceleration → closed-loop handoff | Validate all 20 cold starts; tune current/time after protection exists |

The original planning estimates suggest substantial dead-time-compensation benefit (30%–50% ripple reduction), but no such improvement has been measured. Fixed dead time occupies 2% of the period; Hall sectors span 60 electrical degrees, making both useful measurement priorities.

## 4. P3: execution time and robustness

| ID | Work | Method / validation |
|---|---|---|
| 3.1 | CORDIC sin/cos | Compare against software; estimated 100–200 cycles saved, verify latency/jitter with DWT |
| 3.2 | ISR in SRAM | Dedicated section and linker placement; compare max−min timing, follow project configuration rules |
| 3.3 | d/q feedforward | `vd+=−ωLq·iq`; `vq+=ωLd·id+ωψf`; inspect high-speed load steps; ~30 cycles estimate |
| 3.4 | Flash access | Check G474 acceleration/wait states; compare GPIO timing with DWT |
| 3.5 | Higher PWM | Consider 20→30 kHz only after checking optocoupler edges, switching losses, dead-time ratio, sampling windows |
| 3.6 | Optional sensorless fusion | BEMF observer + Hall complementary filter; compare iq ripple and complexity |

## 5. Deferred options

Field weakening is not an initial 24 V/3000 rpm requirement. MTPA has little saliency benefit under the surface-PM `Ld≈Lq` model. Hall sensors remove the immediate need for zero-speed HFI. A three-shunt board does not need single-shunt reconstruction. Overmodulation sacrifices sampling windows. Start with fixed dead-time compensation before adaptive variants. These are choices to revisit with measurements, not universal conclusions.

## 6. Approaches to avoid

- ISR `printf`, unbounded work, expensive division/large tables without timing justification.
- Feeding noisy instantaneous Hall speed differences directly to the speed PI.
- Raising PWM to 50 kHz without validating optocoupler/driver delays and usable windows.
- Discarding a phase measurement merely to save a channel; retain diagnostic redundancy where windows are valid.
- Tuning PI to hide dead-time distortion instead of characterizing it.

## 7. Milestones

```text
v0.1  Six-step bring-up: phase order, Hall sequence, direction, dead time
G474  Board verified → parallel ADCs → timing/protection acceptance (pending)
v0.2  Current loop: all seven P1 prerequisites
v0.3  Speed loop and P2 motion-quality work
v0.4  Measured P3 improvements, fault state machine, watchdog
v1.0  Documentation, parameters, calibration, regression package
```

Current G474 drive remains locked; milestones above do not imply completed motor operation. Each version should record phase-current/iq/speed-step waveforms, measured cycles/jitter, and what changed/why in both documentation languages.

## 8. Evidence for an effective optimization

| Plot | Inspect | Candidate target |
|---|---|---|
| Phase current | Sinusoidal shape, spikes, steps | No obvious steps, THD <5% |
| iq step | Rise, overshoot, steady ripple | Overshoot <10%, ripple <5% |
| Speed step | Overshoot/recovery | Overshoot <5%, no sustained oscillation |

| Metric | Target | Measured |
|---|---|---|
| Average ISR cycles | <2500 | Pending |
| ISR max−min jitter | <5% | Pending |
| Minimum stable speed | 50 rpm | Pending |
| No-load current at 3000 rpm | Approximately 0.4 A | Pending |
