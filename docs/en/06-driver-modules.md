# 06 Driver modules

**English** | [简体中文](../zh-CN/06-driver-modules.md) | [Documentation index](../README.md)

CubeMX initialization precedes `app_init()`. Core application/shutdown hooks stay in USER CODE regions. G474 is the sole platform: `bsp/board` supplies policy/parameters; ADC implementations and interfaces both live in `bsp/adc_cur/` and `bsp/adc_vbus/`. Layering rules: [13-repo-restructure.md](13-repo-restructure.md).

## 1. PWM: bsp/pwm

| API | Behavior and constraints |
|---|---|
| `bsp_pwm_init()` | Disable bridge, stop outputs, clear duty |
| `bsp_pwm_start()` | While bridge is off, reset count/load preloads, then enable six outputs, MOE, counter |
| `bsp_pwm_check_ready()` | Three-phase self-check; all six outputs enabled in pairs |
| `bsp_pwm_set_duty(du,dv,dw)` | FOC interface; clamp 5%–92% |
| `bsp_pwm_set_duty_raw(du,dv,dw)` | Allow initialization to zero, maximum 92%; running six-step maintains equal nonzero duties on all phases |
| `bsp_pwm_apply_pair(high,low)` | Prepare all phase modes/enables and commit with one COM; 1 success / 0 rejection, caller must stop on failure |
| `bsp_pwm_enable_bridge(en)` | Three-phase enable or unconditional disable; 1 success / 0 rejection |
| `bsp_pwm_enable_bridge_sixstep(high,low)` | Require exact selected two phase pairs and third phase floating; rejection leaves bridge off |
| `bsp_pwm_is_bridge_enabled()` | Software shadow; does not prove hardware overcurrent latch reset |
| `bsp_pwm_stop()` | Bridge off, clear MOE/AOE and outputs, stop counter |
| `bsp_pwm_stop_emergency()` | GPIO BRR lowers SD, then clears TIM1 outputs/counter; no HAL handle/delay dependency |

### Startup and interlocks

G474 refuses bridge enable. The retained path is bridge off → stop PWM → wait 200 ms → prepare equal phase duties → start → three-phase check → `alg_sixstep_apply` → six-step enable check. With MOE off, startup enables CCPC, disables external COM triggering and restores all phases to PWM1; mode/duty commits precede MOE/CEN enable. Rejection returns STOP.

Both checks validate MOE/CEN, ARR=4250, PSC=0, RCR=1, center-aligned mode 1, divider 1, DTG=149, OSSI/OSSR, CCR preload and active-high polarity, plus CCPC=1, CCUS=0, UDIS=0 and low idle levels on all phases. The three-phase check requires PWM1 and mask `0x555`; six-step requires the selected pair mask, PWM1 on the high phase, forced inactive on the others, and equal nonzero CCR values.

COM and update events commit mode, enable and CCR preloads before bridge enable. Running commutation generates only COM, without UG or counter reset. Hardware-triggered ADC work must recheck sampling effects. CCxE/CCxNE permit complementary outputs, not simultaneous conduction. HAL main and complementary starts are separate; this driver explicitly manages all six.

### COM output combinations and calling contract

| Role | OCxM | CCxE / CCxNE | Output |
|---|---|---|---|
| High phase | PWM1 | 1 / 1 | High-side PWM, complementary low side, hardware dead time |
| Low phase | Forced inactive | 1 / 1 | Inactive OCREF: high side off, low side on |
| Floating phase | Forced inactive | 0 / 0 | Both outputs disabled |

The algorithm submits one combination through the application-injected `apply_pair` callback. Failure stops power/PWM and returns STOP. All CCRs maintain usable duty values in advance. Duty changes take effect at the next permitted UEV; COM commits only modes/enables. Commutation before that UEV uses the previous complete duty set. COM is not automatically aligned to the carrier boundary; output edges also depend on CNT and dead time.

Only one main-loop caller may write outputs; concurrent ISR configuration is unsupported. CCR conversion precedes a short UDIS-protected group write that restores the previous UDIS state. The counter keeps running, but a coincident update event is suppressed, potentially delaying update/TRGO. Synchronous ADC or current-loop integration must redesign the update window.

Hardware validation must keep CTRL_SD=0 while observing all six outputs: exercise all forward/reverse steps, constant low-side output, floating phase, duty changes, restart and fault shutdown. Check complementary dead time around COM and carrier continuity. The host model validates UEV/COM register commits only, not electrical waveforms or the power stage.

### Fault shutdown

NMI, HardFault, MemManage, BusFault, UsageFault, and `Error_Handler` lower SD, clear MOE/AOE, clear all enables and commit COM, then stop the counter before halting; shutdown never waits for a natural update. Clearing AOE prevents later update events from restoring MOE. Normal stop also clears pending enables. Reset is required after fatal faults. Software shutdown cannot act after the CPU completely loses execution ability.

## 2. Phase current: bsp/adc_cur

| API | Contract |
|---|---|
| `bsp_adc_cur_init()` | Call with bridge off; platform owns handles; 1 success / 0 failure; remain off and reset after failure |
| `bsp_adc_cur_read(out)` | 1 success; 0 leaves output unchanged, but the old sample must not be reused |

Initialize by calibrating ADC1/2/3 sequentially; any failure leaves the driver unready. CubeMX selects PC0/IN6, PC1/IN7, PB1/IN1. For each ADC: software start → poll (1 ms timeout argument) → read → stop. Publish only after all three succeed; otherwise the application disables power and latches start lockout.

Scaling is `I=(raw×3.3/4095−1.25)/0.12`. HAL calibration does not calibrate external amplifier offset; actual VDDA and per-phase zero remain to be measured. The 500 ms diagnostic acquisition is not synchronized to low-side conduction and is not reliable instantaneous feedback or fast protection. The 8 A limit is deferred pending synchronized sampling.

## 3. Hall inputs: bsp/hall

PC6/PC7/PC8 use pull-ups; U/V/W occupy bits 2/1/0. `bsp_hall_read()` returns 1–6, with 000/111 mapped to `HALL_INVALID`; `bsp_hall_get_state_str()` formats raw state. Read, reject invalid state, then call `bsp_hall_get_sector_deg`.

| Raw code | 1 | 3 | 2 | 6 | 4 | 5 |
|---|---|---|---|---|---|---|
| Relative angle | 0° | 60° | 120° | 180° | 240° | 300° |

See [review evidence](08-review-findings.md) for vendor bit-order conversion. S1 is relative zero only; direction/electrical offset remain uncalibrated. Open-loop code does not call this angle function. Invalid input returns 0°, so angle alone cannot prove validity.

## 4. Debug UART: bsp/uart + app/log

Hardware and business are separated:

| Module | API | Responsibility |
|---|---|---|
| `bsp/uart` | `bsp_uart_init()` | Clear ORE, reset the TX busy flag |
| `bsp/uart` | `bsp_uart_write(data, len)` | Start one USART3_TX DMA transfer; no copy, no wait |
| `bsp/uart` | `bsp_uart_is_tx_busy()` | Query whether DMA is still transmitting |
| `bsp/uart` | `bsp_uart_read_byte(out)` | Nonblocking one-byte RX with overrun recovery |
| `bsp/uart` | `HAL_UART_TxCpltCallback()` | TC interrupt only clears the busy flag |
| `bsp/uart` | `HAL_UART_ErrorCallback()` | Clear busy on TX DMA failure; RX errors do not release the TX buffer |
| `app/log` | `app_log_init()` | Reset the 512-byte static queue |
| `app/log` | `app_log_write(fmt,...)` | Format and enqueue; max 127 bytes per line, drop the whole line when full |
| `app/log` | `app_log_task()` | Main-loop pump: after the previous chunk completes, hand the oldest contiguous span to `bsp_uart_write()` |

`bsp/uart` has no queue, no formatting and **no `_task()`**; main-loop progress belongs to the application (`app/log`).

**TX path (DMA)**: CubeMX configures DMA1_Channel2 with the DMAMUX request `USART3_TX` (NORMAL, byte aligned, memory increment) and enables both DMA1_Channel2 and USART3 interrupts at priority 15 (lowest). `app_log_task()` pumps when `bsp_uart_is_tx_busy()` is 0: advance the tail by the previous chunk length, then hand the contiguous head span to `bsp_uart_write()`. A span wrapping the buffer end is sent in two chunks. The TC callback in the BSP only clears the busy flag; the application polls it, so the BSP never calls back into the application.

**TX errors**: USART3 currently uses DMA only for TX. After HAL terminates a failed DMA transmission, the error callback only clears busy. The main loop releases the original chunk and sends the next one without retrying the failed chunk, so its remaining bytes may be lost. RX overrun and errors on other UARTs do not clear TX busy or release a buffer still being transmitted. Host regression covers continued transmission after a DMA error and isolation from RX errors; verify log recovery and independent reception with error injection on hardware while keeping the bridge disabled.

`app_log_task()` times reclamation and DMA submission only after confirming there is queue work. Empty queues and DMA-busy returns fall into `poll`. A rejected submission still counts as a work attempt; later main-loop iterations retry it. The BSP has no dependency on application performance accounting.

## 5. Application and limitations

Commands: `0` stop; `1`/`2` forward/reverse (rejected today); `h` Hall monitor; `+`/`-` duty; `>`/`<` electrical frequency; `?` help. Help is split to avoid truncation.

Sequence: U+V−, U+W−, V+W−, V+U−, W+U−, W+V−. Only the floating phase is disabled; high phase is chopped, low phase has 0% high-side duty. Ranges: 5%–60%, 0.5–20 electrical Hz. Millisecond main-loop scheduling adds quantization/jitter. No current loop, synchronous sampling, or software current limiter is running.

The application layer is split by responsibility: `app/app` (init, mode interlocks, main loop, shared state), `app/cmd` (commands), `app/control` (algorithm strategy), `app/log` (log queue), `app/perf` (performance), `app/status` (status output). Every subfolder exposes a `*_task()` entry called from `app_task()`.

## 6. Six-step strategy and algorithm: app/control + algorithm/sixstep

| Module | Responsibility |
|---|---|
| `algorithm/sixstep` | Step table, next-step calculation, apply one step. **Pure algorithm**: standard C only; hardware output goes through injected `alg_sixstep_io_t` callbacks |
| `app/control` | Six-step parameters (step, duty, electrical frequency) and bench limits; injects the BSP `bsp_pwm_apply_pair/set_duty_raw` callbacks; `app_control_task()` advances at the step interval |

```text
app_task → app_control_task → alg_sixstep_next_step / alg_sixstep_apply
                                      │ callbacks
                                      ▼
                              bsp_pwm_apply_pair / bsp_pwm_set_duty_raw
```

The algorithm layer includes no BSP/application header, so it can be host-tested without hardware and the same callback mechanism can serve FOC later. The bridge interlock still calls `bsp_pwm_enable_bridge_sixstep()` from `app/control`.

See [validation](09-validation-and-maintenance.md) for hardware verification.
