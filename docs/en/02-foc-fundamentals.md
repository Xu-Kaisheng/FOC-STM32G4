# 02 FOC fundamentals and data flow

**English** | [简体中文](../zh-CN/02-foc-fundamentals.md) | [Documentation index](../README.md)

An introduction using STM32G474RET6 at 170 MHz, ATK-PD6010B with 20 mΩ low-side shunts, and BL57H95 (4 poles, 24 V, 6.6 A). **The following control chain is a design plan, not running firmware.** Timing and optimization figures are estimates unless explicitly identified as measurements.

## 1. Why FOC changes coordinates

FOC transforms three-phase AC quantities into two approximately DC quantities in a reference frame rotating with the rotor, controls them, then transforms the result back.

At 3000 rpm and 2 pole pairs, electrical current has a 100 Hz fundamental. PI control can remove steady-state error for a constant reference in an ideal unsaturated model; directly tracking AC introduces frequency-dependent amplitude/phase error. Rotating coordinates make steady operating-point current references constant.

## 2. Coordinate transforms

```text
Three stationary phases      Two stationary axes      Two rotating axes
       a / b / c     ─Clarke→       α / β       ─Park→       d / q
```

For balanced three-phase currents, `ia+ib+ic=0`, leaving two independent quantities:

```text
iα = ia
iβ = (ia + 2·ib) / √3

id =  iα·cosθ + iβ·sinθ
iq = −iα·sinθ + iβ·cosθ
```

θ is rotor electrical angle. The d axis aligns with rotor flux; q leads d by 90°.

| Axis | Role | Surface-PM starting strategy |
|---|---|---|
| d | Flux/excitation | `id*=0` below field-weakening operation |
| q | Torque | `iq*` supplied by the speed loop |

```text
T = 1.5·p·[ψf·iq + (Ld−Lq)·id·iq]
Ld≈Lq → T≈1.5·p·ψf·iq
```

Thus q current controls torque under this model. Rated motor values suggest `KT≈0.05 N·m/A` and 6.6 A for 0.33 N·m, but actual q-axis scaling depends on peak/RMS conventions and motor characterization.

## 3. Proposed data flow per 50 µs PWM period

```text
TIM1 event → synchronized ADC1/2/3 (pending) → conversion-complete ISR
  1. Read ADCs and scale current: I=(V−1.25)/0.12
  2. Clarke: ia/ib → iα/iβ
  3. Obtain θ from calibrated Hall sector and interpolation
  4. Park: id/iq using sinθ/cosθ
  5. Two current PIs: vd/vq (id*=0, iq* from speed loop)
  6. Inverse Park: vα/vβ, reuse the same sin/cos
  7. SVPWM: Ta/Tb/Tc
  8. Write TIM1 CCR1/2/3
Next update event commits duty preloads
```

The actual trigger event and low-side sampling window must be verified; a diagram labeled “underflow” alone does not configure them.

| Step | Operation | Hardware | Illustrative cost |
|---|---|---|---|
| Acquisition | Common trigger, three ADCs | ADC1/2/3, pending | Window and delay unmeasured |
| Scaling | `(V−1.25)/0.12` | CPU | ~30 cycles |
| Clarke | Two multiplies, one add | CPU | ~10 cycles |
| Angle | Sector + `ω·Δt` | Future Hall edge timing | Correction every 1.67 ms at 3000 rpm |
| Park | Four multiplies, two adds, sin/cos | CPU / optional CORDIC | Arithmetic ~20–30 cycles, trig extra |
| PI | Two controllers | CPU | ~40 cycles |
| Inverse Park | Reuse sin/cos | CPU | ~20 cycles |
| SVPWM | Phase voltages and min/max offset | CPU | ~60 cycles |
| CCR writes | Three preload registers | TIM1 | ~10 cycles |

At 170 MHz, the total period budget is **8500 cycles**. The loop is not implemented, so total execution time and utilization remain unmeasured. Start with software math and measure before adding accelerators.

### SVPWM as zero-sequence injection

```c
// Convert stationary two-axis voltage to three phase references.
va = valpha;
vb = -0.5f * valpha + 0.866f * vbeta;
vc = -0.5f * valpha - 0.866f * vbeta;

// Center the phase extrema to improve linear bus utilization.
vmax = max3(va, vb, vc);
vmin = min3(va, vb, vc);
offset = -0.5f * (vmax + vmin);

// Repeat for b/c and enforce the sampling/duty limits.
duty_a = 0.5f + (va + offset) / Vdc;
```

The min/max offset is equivalent to space-vector modulation in the linear range and offers about 15% more voltage utilization than basic sinusoidal modulation. This project retains a 5%–92% duty clamp.

## 4. CORDIC

Coordinate Rotation Digital Computer uses iterative shifts/adds and a small angle table. STM32G4 provides a hardware accelerator (DS12288 Rev 6 §3.8).

| Item | Capability |
|---|---|
| Engine | 24-bit rotation engine, circular/hyperbolic operations |
| Modes | Rotation for sin/cos; vectoring for angle/magnitude |
| Functions | sin, cos, sinh, cosh, atan, atan2, atanh, magnitude, sqrt, ln |
| Precision | Configurable iteration count |
| Interface | AHB slave, register reads, optional DMA |

FOC primarily needs `θ→sinθ/cosθ`; future observers may also use atan2 and magnitude. Register-read readiness and latency must follow the reference manual; an accelerator still has a data-dependency delay.

| Approach | Illustrative cycles | Tradeoff |
|---|---:|---|
| `sinf()` / `arm_sin_cos_f32()` | 100–300 | Depends on implementation, compiler, memory access |
| Lookup table | 20–40 | Table storage and memory latency |
| CORDIC | 30–60 | Predictable configured latency; peripheral interface overhead |

Usage involves writing WDATA and reading results in the specified order, often cosine then sine. Verify normalization, readiness, and precision. It is feasible in a 20 kHz loop, but not required for the first implementation.

## 5. FMAC

Filter Math Accelerator is a multiply-accumulate engine with address generation (DS12288 Rev 6 §3.9).

| Item | Capability |
|---|---|
| Multiplier | 16×16-bit |
| Accumulator | 24+2-bit |
| Local memory | 256×16-bit for coefficients/state |
| Buffers | Up to three (two inputs, one output), circular support |
| Operations | FIR, direct-form-I IIR, dot product, convolution, correlation |
| Interface | AHB/DMA, can run alongside CPU |

FMAC is optional filtering support, not the vector-control chain itself. The board already has a 1 kΩ/1 nF analog RC; assess noise before adding current filtering. Candidate uses include a second-order IIR for iq feedback, Hall-speed smoothing/differentiation, and mechanical-resonance notch filters.

## 6. Adoption order

| Stage | Trigonometry | Filtering | Reason |
|---|---|---|---|
| First working current loop | Software sin/cos | Only what measurements require | Establish correctness |
| Measure cycles/jitter | Compare CORDIC | Measure before adding | Decide using DWT data |
| Observer/sensorless work | Evaluate CORDIC atan2 | Evaluate FMAC | Extra workload may justify hardware |

Reconsider acceleration if measured ISR cost exceeds 5000 cycles, PWM rises to 30–40 kHz, observer/field-weakening work is added, or jitter exceeds 5%. These are investigation triggers, not relaxations of the <15 µs target / <50 µs hard deadline.

The main chain is current acquisition → Clarke → Park → PI → inverse Park → SVPWM → CCR. Sin/cos acceleration and filtering are optional implementation choices. Obtaining a calibrated, timely rotor angle is central to correctness.
