# 01 Hardware and wiring

**English** | [简体中文](../zh-CN/01-hardware-and-wiring.md) | [Documentation index](../README.md)

The only controller is STM32G474RET6 on DevEBox Ver.20. The board schematic and DS12288 Rev 6 have been checked. Header numbers and photograph orientation are in the [G474 board guide](11-g474-board-guide.md).

## 1. Topology and status

24 V supply → ATK-PD6010B → motor U/V/W. Motor Hall signals go to CN2; CN3 connects six PWM inputs, CTRL_SD, three phase-current outputs, VBUS, and Hall signals. Analog inputs are not isolated; MCU and driver board must share ground.

Current firmware provides GPIO Hall polling, sequential ADC1/2/3 reads, ADC4 bus monitoring, and UART performance reports with the bridge disabled. The boot PWM demo has been tested and disabled. Current control, synchronized acquisition, and fast 8 A limiting are pending.

## 2. Driver-board circuits and scaling

| Item | Interpretation | Source |
|---|---|---|
| PWM polarity | High input commands the high-side switch on; noninverting | PWM_UH → TLP2355 anode pin 1; cathode pin 3 → 220 Ω → GND; manual §2.1.1 |
| Input current | Approximately `(3.3−1.5)/220 ≈ 8 mA` per MCU output | R5/R21/etc. are 220 Ω; LED anode driven by MCU |
| Half bridges | Six independent inputs; **MCU must generate dead time** | Separate optocouplers feed IR2110 HIN/LIN |
| Phase current | `V_AMP=1.25+0.12×I`; `I=(V−1.25)/0.12` | 20 mΩ shunt, gain 6, 1.25 V bias |
| Current range | 0–3.3 V represents about −10.4 to +17.1 A; hardware trip 10.4 A, 0.5 A hysteresis | §2.1.4 |
| Phase voltage/BEMF | `Motor_x=25×V_BEMF` | 12 kΩ + 12 kΩ + 1 kΩ divider; §2.1.3 |
| Bus voltage | `POWER=25×V_VBUS`; nominal ADC full scale 82.5 V | §2.1.11 |
| Temperature | `VTEMP=3.3×4.7/(Rt+4.7)`; Rt in kΩ, 10 kΩ NTC at 25 °C | §2.1.11 |
| Shutdown | CTRL_SD=1 enables bridge; 0 disables it | §2.1.5 |
| Overcurrent | With R70 fitted, shutdown is latched; lower CTRL_SD to reset | §2.1.5 |
| Hall/zero crossing | JP3 1–2: Hall (default); 2–3: zero crossing | §2.1.8 |
| CN3 pin 5 | JP1 1–2: ENCZ; 2–3: R_BREAK | §2.1.7 |
| Encoder supply | JP2 1–2: 5 V (default); 2–3: 11 V | §2.1.9 |

The driver-board analog supply and MCU VDDA use different regulators. Reference mismatch affects current gain and offset. VREFINT-based VDDA calibration is planned; scaling still assumes 3.3 V.

## 3. Motor: BL57H95 / 57BLY95-230-001

| Parameter | Value | Parameter | Value |
|---|---|---|---|
| Phases | 3 | Poles | **4, or 2 pole pairs** |
| Rated voltage | 24 VDC | No-load speed | 4000 rpm |
| Rated speed | 3000 rpm | No-load current | 0.4 A |
| Rated torque | 0.33 N·m | Rated current | 6.6 A |
| Rated power | 105 W | Insulation / mass | Class B / 1.0 kg |
| Leads | U/V/W and VENC(+5 V)/DGND/HALU/HALV/HALW | Encoder | None; no A/B/Z |

The motor has a 57 mm flange and 95 mm body. At 3000 rpm it runs at 50 mechanical rps and 100 electrical Hz. An electrical period is 10 ms; a Hall sector is 1.67 ms, about 33 periods at 20 kHz. Interpolation still needs direction/offset calibration and a speed estimate.

`KT≈0.33/6.6=0.05 N·m/A`. A rough no-load estimate is `Ke≈24/4000=6 V/krpm`; verify conventions and actual values. Phase resistance/inductance are absent from the supplied specification. Measure line-to-line quantities and convert for the actual winding connection; `Rline=2Rphase` and `Lline≈2Lphase` are starting relations for the assumed connection. Typical 57 mm motor values of 0.5–1 Ω and 1–2 mH are not measurements of this motor.

A stalled motor or voltage-first open-loop start can hit the 10.4 A latch. Planned startup needs a software limit ≤8 A, a current-reference ramp, and alignment before closed-loop operation. A duty limit alone does not implement those protections.

## 4. Connections

### 4.1 Power

| Terminal | Connection | Notes |
|---|---|---|
| CN1 VIN | Supply +24 V | Short cable; suggested cross-section ≥1.5 mm² |
| CN1 GND | Supply negative | Connect MCU ground separately |
| CN1 Motor_U/V/W | Motor U/V/W | Preserve labels and Hall correspondence; change direction in software |
| CN4 | Optional braking resistor | Initial suggestion 20 Ω / 100 W; size for actual braking energy |

A 24 V / 10 A supply is suggested. Initial checks may use a current-limited 12 V supply within driver ratings. Preserve the phase/Hall relationship when changing direction in software.

### 4.2 Hall connector CN2

CN2 is 1×8 HT396R. Match signal names, not wire order.

| Pin | Board signal | Motor lead | Notes |
|---:|---|---|---|
| 1 | HALL W | HALW | 1 kΩ series, 4.7 kΩ pull-up, 102 filter capacitor |
| 2 | HALL V | HALV | Same network |
| 3 | HALL U | HALU | Same network |
| 4 | GND | DGND | Route separately from high-current return |
| 5 | VENC | VENC | 5 V with JP2 at 1–2 |
| 6 / 7 / 8 | ENC Z/B/A | Disconnected | Motor has no encoder |

CN2 order is W/V/U/GND/VENC. Measure CN3-2/4/6 high levels before MCU connection and check G474 input conditions. Hall supply voltage alone does not establish signal voltage.

### 4.3 CN3 to G474

| CN3 | Signal | MCU | Function |
|---:|---|---|---|
| 2 / 4 / 6 | HALL U/V/W | PC6/PC7/PC8 | Pull-ups, main-loop polling |
| 8 | VBUS | PB12 | ADC4_IN3, ×25 |
| 10 / 12 / 14 | AMP_IU/IV/IW | PC0/PC1/PB1 | ADC1_IN6 / ADC2_IN7 / ADC3_IN1 |
| 17 / 18 | PWM_UH/UL | PA8/PB13 | TIM1_CH1/CH1N |
| 19 / 20 | PWM_VH/VL | PA9/PB14 | TIM1_CH2/CH2N |
| 21 / 22 | PWM_WH/WL | PA10/PB15 | TIM1_CH3/CH3N |
| 24 | CTRL_SD | PC9 | Active high, initialized low, enable prohibited |
| 7 / 16 | GND | GND | Connect both |
| 15 | VTEMP | Unconfigured | Leave disconnected |
| 23 | 5V_IN | **Do not connect** | Prevent backfeeding |
| — | Debug TX/RX | PB10/PB11 | USART3, 115200 8N1, 3.3 V logic |

**J3 must remain empty**: OLED/TFT shares PB12–PB15 and PC6/PC7. Flash PA15/PC10/PC11/PC12, LEDs PC13/PD2, and buttons PA0/PA1 do not conflict. Reserve USB PA11/PA12, SWD PA13/PA14, and oscillator pins PF0/PF1 and PC14/PC15.

PB10 → USB–TTL RX; PB11 → USB–TTL TX; common ground. Driver CN3-8 is divided analog VBUS, whereas development-board USB VBUS is a 5 V power net. Never join them. Leave CN3-1/3/5 encoder and 9/11/13 BEMF disconnected.

### 4.4 Jumpers

| Jumper | Default | Meaning |
|---|---|---|
| JP1 | 1–2 | CN3-5 ENCZ, unused |
| JP2 | 1–2 | Hall/encoder supply 5 V |
| JP3 | 1–2 | CN3-2/4/6 Hall signals |

## 5. Parameters and validation order

HSI+PLL runs at 170 MHz. TIM1 is center-aligned at 20 kHz with PSC=0, ARR=4250, RCR=1. Clock divider 1 and DTG=149 give 1 µs dead time. ADC1/2/3/4 use 42.5 MHz, 12-bit, 247.5 sampling cycles, regular software triggering. Monitoring is every 500 ms with a 1 ms polling timeout argument. VBUS uses `raw×3.3×25/4096`, without voltage protection or reference calibration. Future current/speed targets are 20 kHz / 1 kHz, or 8500 CPU cycles per current period.

Check headers, grounds, and empty J3 first. Initially connect only debugger/UART. With the bridge off, check analog voltages: 24 V bus gives about 0.96 V; zero current about 1.25 V. Turn the motor by hand to inspect Hall states.

On 2026-11-17 the user reported normal PWM and **990 ns dead time**. The demo is disabled; the 1 µs setting remains and PC9 stays low. A future recheck requires explicitly enabling a bridge-off test, inspecting each pair for a 50 µs period and about 1 µs both-low gap. Compilation and slow ADC reads do not validate loaded operation. Keep phase/Hall wiring fixed; 8 A limiting is pending. See [validation](09-validation-and-maintenance.md).

The COM six-step interface maintains equal phase CCR duties and commits high-phase PWM1, low-phase forced inactive and the third phase disabled together. Bridge-off validation must cover all six outputs, forward/reverse commutation, constant low-side conduction, floating phase, dead time and carrier continuity. Earlier temporary PWM-demo results do not validate COM commutation. See the [PWM driver](06-driver-modules.md) for interface constraints.
