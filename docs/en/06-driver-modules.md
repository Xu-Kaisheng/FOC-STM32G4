# 06 Driver modules

**English** | [简体中文](../zh-CN/06-driver-modules.md) | [Documentation index](../README.md)

CubeMX initialization precedes `app_init()`. Core application/shutdown hooks stay in USER CODE regions. G474 is the sole platform: `app_board` supplies policy/parameters; ADC implementations live in `platform/`, with interfaces in `src/drv/`.

## 1. PWM: tim_pwm

| API | Behavior and constraints |
|---|---|
| `pwm_init()` | Disable bridge, stop outputs, clear duty |
| `pwm_start()` | While bridge is off, reset count/load preloads, then enable six outputs, MOE, counter |
| `pwm_check_ready()` | Three-phase self-check; all six outputs enabled in pairs |
| `pwm_set_duty(du,dv,dw)` | FOC interface; clamp 5%–92% |
| `pwm_set_duty_raw(du,dv,dw)` | Six-step interface; allow 0% for low phase, maximum 92% |
| `pwm_phase_enable(phase,en)` | Change that phase's CCxE/CCxNE only; do not stop counter |
| `pwm_bridge_enable(en)` | Three-phase enable or unconditional disable; 1 success / 0 rejection |
| `pwm_bridge_enable_sixstep(high,low)` | Require exact selected two phase pairs and third phase floating; rejection leaves bridge off |
| `pwm_bridge_is_enabled()` | Software shadow; does not prove hardware overcurrent latch reset |
| `pwm_stop()` | Bridge off, clear MOE/AOE and outputs, stop counter |
| `pwm_emergency_stop()` | GPIO BRR lowers SD, then clears TIM1 outputs/counter; no HAL handle/delay dependency |

### Startup and interlocks

G474 refuses bridge enable. The retained path is bridge off → stop PWM → wait 200 ms → clear duty → start → three-phase check → `apply_step` → six-step enable check. Rejection returns STOP.

Both checks validate MOE/CEN, ARR=4250, PSC=0, RCR=1, center-aligned mode 1, divider 1, DTG=149, OSSI/OSSR, PWM1, CCR preload, and active-high polarity. The three-phase mask must be `0x555`; the six-step mask must match the specific selected phases.

An update event commits CCR preload before bridge enable, preventing stale duty. Hardware-triggered ADC work must recheck this event's sampling effects. CCxE/CCxNE permit complementary outputs, not simultaneous conduction. HAL main and complementary starts are separate; this driver explicitly manages all six. Floating a phase must not invoke a HAL stop that also changes MOE/counter.

### Fault shutdown

NMI, HardFault, MemManage, BusFault, UsageFault, and `Error_Handler` stop power before halting. Clearing AOE prevents later update events from restoring MOE. Reset is required. Software shutdown cannot act after the CPU completely loses execution ability.

## 2. Phase current: adc_cur

| API | Contract |
|---|---|
| `adc_cur_init()` | Call with bridge off; platform owns handles; 1 success / 0 failure; remain off and reset after failure |
| `adc_cur_read(out)` | 1 success; 0 leaves output unchanged, but the old sample must not be reused |

Initialize by calibrating ADC1/2/3 sequentially; any failure leaves the driver unready. CubeMX selects PC0/IN6, PC1/IN7, PB1/IN1. For each ADC: software start → poll (1 ms timeout argument) → read → stop. Publish only after all three succeed; otherwise the application disables power and latches start lockout.

Scaling is `I=(raw×3.3/4095−1.25)/0.12`. HAL calibration does not calibrate external amplifier offset; actual VDDA and per-phase zero remain to be measured. The 500 ms diagnostic acquisition is not synchronized to low-side conduction and is not reliable instantaneous feedback or fast protection. The 8 A limit is deferred pending synchronized sampling.

## 3. Hall inputs

PC6/PC7/PC8 use pull-ups; U/V/W occupy bits 2/1/0. `hall_read()` returns 1–6, with 000/111 mapped to `HALL_INVALID`; `hall_state_str()` formats raw state. Read, reject invalid state, then call `hall_sector_deg`.

| Raw code | 1 | 3 | 2 | 6 | 4 | 5 |
|---|---|---|---|---|---|---|
| Relative angle | 0° | 60° | 120° | 180° | 240° | 300° |

See [review evidence](08-review-findings.md) for vendor bit-order conversion. S1 is relative zero only; direction/electrical offset remain uncalibrated. Open-loop code does not call this angle function. Invalid input returns 0°, so angle alone cannot prove validity.

## 4. Debug UART

| API | Behavior |
|---|---|
| `bsp_uart_init()` | Initialize static TX queue, clear ORE |
| `bsp_uart_printf(fmt,...)` | Format/enqueue at most 127 bytes; do not wait for transmission |
| `bsp_uart_task()` | At most one TX byte per main-loop iteration; return if busy/empty |
| `bsp_uart_read(out)` | Nonblocking one-byte RX and overrun recovery; 1 received / 0 no data or invalid argument |

The 512-byte ring holds 511 bytes. Insufficient space drops the entire log. APIs are main-loop-only, not concurrently callable from ISRs. Formatting still costs CPU time. Mode transitions wait 200 ms without reception; bursts may be lost. Clearing ORE recovers subsequent reception, not lost bytes. A nonblocking recovery state machine is future work. The dormant boot-test loop drains logs while waiting.

## 5. Application and limitations

Commands: `0` stop; `1`/`2` forward/reverse (rejected today); `h` Hall monitor; `+`/`-` duty; `>`/`<` electrical frequency; `?` help. Help is split to avoid truncation.

Sequence: U+V−, U+W−, V+W−, V+U−, W+U−, W+V−. Only the floating phase is disabled; high phase is chopped, low phase has 0% high-side duty. Ranges: 5%–60%, 0.5–20 electrical Hz. Millisecond main-loop scheduling adds quantization/jitter. No current loop, synchronous sampling, or software current limiter is running. See [validation](09-validation-and-maintenance.md).
