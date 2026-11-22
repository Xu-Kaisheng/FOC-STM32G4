# 05 Runtime design: load, loop rates, and filtering

**English** | [简体中文](../zh-CN/05-runtime-design.md) | [Documentation index](../README.md)

G474 runs at 170 MHz with nominal 20 kHz PWM. Performance accounting exists; current/speed loops and filtering below are future designs. Cycle counts and control-quality figures are estimates or targets, not measurements.

## 1. Design scope

Frequencies, cycle budgets and filter values below are design examples, not configuration copies. Current capabilities belong to [09](09-validation-and-maintenance.md); implemented accounting interfaces belong to [12](12-performance-monitoring.md). Validate worst-case execution, not average load alone.

## 2. Proposed rates

### 2.1 Current loop

```text
f_cur = f_TIM1clk / (2 × ARR) = 170 MHz / (2 × 4250) = 20 kHz
T_cur = 50 µs = 8500 CPU cycles
```

The factor two comes from up/down center-aligned counting. Select ADC triggers and acquisition time within a valid low-side window; RCR=1 alone does not prove sampling placement. Existing acquisition contracts belong to [06](06-driver-modules.md).

### 2.2 Speed loop

Target `f_spd=1 kHz=f_cur/20`. A current-loop divider can schedule the speed loop every 20 periods. The release phase is then tied to the current loop; execution still has timing overhead and possible preemption.

### 2.3 Hall events

```text
f_hall = (rpm/60) × pole_pairs × 6
3000 rpm, 2 pairs → 50 × 2 × 6 = 600 events/s
```

That is one event per 1.67 ms. Current Hall reads are main-loop polled without a fixed cadence. Future EXTI/timer capture can timestamp edges and support interpolation.

### 2.4 Kalman filter

A candidate prediction/update schedule is 1 kHz, matching the speed loop. Hall events are only 600 Hz at rated speed; faster prediction does not create new measurements. Measurement correction should respect whether fresh edge-derived data exists.

### 2.5 Proposed control architecture, not implemented

| Task | Frequency | Period | Trigger / source | Illustrative cycles |
|---|---|---|---|---|
| Current loop | 20 kHz | 50 µs | Verified TIM1 event → ADC → ISR | 1500–3000 |
| Hall sampling option | 20 kHz | 50 µs | Current-loop sampling, or independent edge capture | ~50 |
| Speed loop + filter | 1 kHz | 1 ms | Divide current-loop schedule | ~500 |
| UART/commands today | Main-loop polling | Variable | No UART RX interrupt | Variable |

## 3. Speed/acceleration Kalman proposal

### 3.1 Problem

Sector timing gives electrical speed from `ω_e=(π/3)/Δt`. Divide by pole pairs for mechanical speed. Timing quantization and Hall-placement error make the estimate jump at boundaries and can excite speed-loop noise, especially at low speed. The proposed filter smooths speed and estimates acceleration for angle prediction; it is not part of current firmware.

### 3.2 Model

Choose `x=[ω,a]ᵀ`, speed in rad/s and acceleration in rad/s², with a consistent mechanical/electrical convention:

```text
ω(k+1) = ω(k) + a(k)·Ts
a(k+1) = a(k)

F = [1 Ts]     H = [1 0]     Q = diag(q_ω,q_a)     R = r
    [0  1]
```

Estimate R from measured Hall-speed variance at constant speed. Q describes model uncertainty; lower acceleration process noise smooths more but tracks load changes more slowly.

### 3.3 Recursion

```text
Predict: x⁻ = F·x;       P⁻ = F·P·Fᵀ + Q
Correct: K  = P⁻·Hᵀ / (H·P⁻·Hᵀ + r)
         x  = x⁻ + K·(z − H·x⁻)
         P  = (I − K·H)·P⁻
```

These are small 2×2/scalar operations, roughly 50 floating-point operations per update as an estimate. Measure cost and numerical behavior before deployment; consider Joseph-form covariance updates for robustness.

### 3.4 Angle prediction

With mechanical speed/acceleration estimates:

```c
/* Ts is in seconds; electrical angle is in radians. */
theta_elec += (omega * Ts + 0.5f * accel * Ts * Ts) * POLE_PAIRS;
theta_elec = wrap_2pi(theta_elec);
```

The acceleration term extends linear interpolation. Its usefulness depends on estimate quality and Hall-edge corrections.

### 3.5 Illustrative implementation

This sketch is not production code; state, constants, valid-measurement gating, and numerical safeguards must be supplied before use.

```c
/**
 * @brief Estimate speed and acceleration from a Hall speed observation.
 * @param z_omega Speed measurement in rad/s, using the filter's convention.
 * @retval None; updates the illustrative kf state.
 */
static void kf_update(float z_omega)
{
    /* Predict a constant-acceleration state and covariance. */
    kf.omega += kf.accel * KF_TS;
    float p00 = kf.p00 + KF_TS * (kf.p10 + kf.p01)
                + KF_TS * KF_TS * kf.p11 + KF_Q_W;
    float p01 = kf.p01 + KF_TS * kf.p11;
    float p10 = kf.p10 + KF_TS * kf.p11;
    float p11 = kf.p11 + KF_Q_A;
    kf.p00 = p00; kf.p01 = p01; kf.p10 = p10; kf.p11 = p11;

    /* Correct using a fresh speed measurement. */
    float s = kf.p00 + KF_R;
    float k0 = kf.p00 / s;
    float k1 = kf.p10 / s;
    float innov = z_omega - kf.omega;
    kf.omega += k0 * innov;
    kf.accel += k1 * innov;

    /* Compact covariance update; not the full Joseph form. */
    float n00 = (1.0f - k0) * kf.p00;
    float n01 = (1.0f - k0) * kf.p01;
    float n10 = kf.p10 - k1 * kf.p00;
    float n11 = kf.p11 - k1 * kf.p01;
    kf.p00 = n00; kf.p01 = n01; kf.p10 = n10; kf.p11 = n11;
}
```

### 3.6 Tuning order

1. Establish constant-speed operation and measure Hall-speed variance to estimate R.
2. Increase Q from small values: too small lags acceleration, too large admits noise.
3. Validate a reference step; candidate targets are <5% overshoot and <200 ms recovery.
4. Observe the filter in parallel before using it as speed feedback.

### 3.7 When a simpler filter is enough

At speeds above roughly 500 rpm, sector timing may already be adequate for the intended application. Low-speed operation below 100 rpm and acceleration estimation are stronger reasons to investigate Kalman filtering. Start with a first-order low-pass for v0.2; compare the same recorded step data before adopting the v0.3 filter.

## 4. Future integration

```text
Verified TIM1 event → ADC results → 20 kHz current ISR
  ├─ Clarke / Park / PI / SVPWM → CCR
  ├─ Hall sampling or captured edge timestamps
  ├─ Every 20 periods: speed task
  │    ├─ Hall timing → fresh speed observation
  │    ├─ Filter → speed / acceleration estimate
  │    └─ Speed PI → iq reference
  └─ DWT timing markers

Main loop → UART commands/logs and once-per-second performance reports
```
