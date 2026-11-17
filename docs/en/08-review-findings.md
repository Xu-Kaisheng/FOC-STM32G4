# 08 Review findings

**English** | [简体中文](../zh-CN/08-review-findings.md) | [Documentation index](../README.md)

## 1. Complete G474 ADC samples

ADC1/2/3 regular groups are read sequentially. Publication occurs only after all three start/poll/read/stop sequences succeed. Failure returns 0 without replacing the caller's object; the caller must treat it as invalid and latch shutdown. ADC4 independently monitors the bus.

Host tests inject failures at each acquisition stage for each current ADC and verify no partial publication. Synchronous triggering, current control, and fast protection are pending.

## 2. Why applying a six-step state could reject bridge enable

Preparing commutation before enabling power is correct. The old check did not match that state:

1. `bsp_pwm_start()` enabled all three pairs, mask `0x555`.
2. First `apply_step()` selected U+V− and floated W, mask `0x055`.
3. `bsp_pwm_enable_bridge(1)` called `bsp_pwm_check_ready()`.
4. The latter required `0x555`, so it rejected enable and left CTRL_SD low.

The implementation now keeps the three-phase self-check and adds `bsp_pwm_enable_bridge_sixstep(high,low)` for the exact selected pair mask and floating third phase. Both verify dead time, period, counter mode, preload, and polarity; preload is committed before power enable. Rejection returns STOP rather than falsely reporting operation. Output-channel enable permits complementary PWM, not simultaneous high/low MOSFET conduction.

## 3. Vendor Hall sequence evidence

The read-only local source archive is:

```text
datasheet/【正点原子】电机驱动板&电机/2，ATK-PD6010B直流无刷驱动板资料/1，实例程序/BLDC_PMSM专题实验（DMF407电机开发板）.zip
```

Inside `实验9 BLDC专题实验/实验9-1 BLDC基础驱动/Drivers/BSP/TIMER/bldc_tim.c`, line 168 states sequence `6,2,3,1,5,4`; line 175 only checks numeric range 1–6; line 177 dispatches `pfunclist_m1[step_sta-1]()`.

The same experiment's `Drivers/BSP/BLDC/bldc.c` lines 107–117 encode U/V/W in bits 0/1/2. This project uses bits 2/1/0. Reordering bits gives `3→2→6→4→5→1`, or cyclically `1→3→2→6→4→5`.

Valid numeric values 1–6 do not imply a motion sequence `1→2→3→4→5→6`. The example dispatches on raw codes; it does not compute angle as `(raw−1)×60°`.

`bsp_hall_get_sector_deg()` now maps `1→3→2→6→4→5` to `0°→60°→120°→180°→240°→300°`. S1 is relative zero only. This bit-order conversion does not calibrate BL57H95 direction or electrical offset. The current open-loop path does not call that angle API.

## 4. Implemented fixes and scope

- `bsp_pwm_stop_emergency()` lowers CTRL_SD before clearing TIM1 MOE/AOE, six output enables, and CEN in fatal exceptions and `Error_Handler`.
- Actual mode changes stop power/PWM before a 200 ms main-loop wait; scheduling time is refreshed afterward.
- 8 A software limiting remains deferred and documented; it is not implemented.
- Fault shutdown latency and physical GPIO timing were not measured during the software review. See [driver guide](06-driver-modules.md) and [validation](09-validation-and-maintenance.md) for hardware checks and the later PWM report.
- UART logs drain from the main loop. Queue overflow drops an entire message; help is split to fit the format buffer.
- Keil rebuilt with 0 errors/warnings, and actual modules passed host regression against HAL substitutes. Review-time Core hooks stayed inside USER CODE regions.
