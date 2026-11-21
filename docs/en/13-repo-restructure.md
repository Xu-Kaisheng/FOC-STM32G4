# 13 Repository Restructure Plan

**English** | [简体中文](../zh-CN/13-repo-restructure.md) | [Documentation index](../README.md)

This document defines the directory restructure of `src/` and `platform/`: target layout, dependency rules, split details, staged migration and acceptance criteria. The plan was implemented on 2026-11-17; status and known gaps are in section 10.

The firmware is still in the bridge-off diagnostic stage. The restructure **changes no control behaviour**: clock, pins, duty clamps, dead time and `CTRL_SD` sequencing are untouched, and no current/FOC loop is added. Only file locations, file granularity and header dependencies change.

---

## 1. Current problems

### 1.1 `platform/` and `bsp/` duplicate the same concept

`src/bsp/` and `platform/` both hold chip/board-specific implementations. By the usual BSP definition they are one layer, yet they were split into two sibling directories, with `platform/` outside `src/`:

| File | Depends on | Nature |
|---|---|---|
| `src/bsp/bsp_uart.c` | `usart.h`, `huart3` | Board implementation |
| `src/bsp/bsp_perf.c` | `main.h`, DWT, `HAL_GetTick` | Board implementation |
| `platform/adc_cur.c` | `adc.h`, `hadc1~3` | Board implementation |
| `platform/adc_vbus.c` | `adc.h`, `hadc4` | Board implementation |
| `platform/app_board.c` | `main.h`, `HAL_GetTick` | Board implementation |

One module is split across two places: `src/drv/adc_cur.h` declares the interface while `platform/adc_cur.c` implements it; same for `adc_vbus`.

### 1.2 `bsp_uart` and `bsp_perf` depend on each other

```text
src/bsp/bsp_uart.c:6    #include "bsp_perf.h"
src/bsp/bsp_uart.c:42   bsp_perf_work_begin();
src/bsp/bsp_uart.c:45   bsp_perf_work_end();

src/bsp/bsp_perf.c:5    #include "bsp_uart.h"
src/bsp/bsp_perf.c:192  bsp_uart_printf("[PERF] ...");
src/bsp/bsp_perf.c:221  bsp_uart_printf("[PERF] CPU work=...");
```

This is a **dependency cycle**, worse than "too many lateral calls": neither module can be compiled or tested alone, and any interface change hits both.

### 1.3 Lower layers include upper-layer headers

```text
src/drv/tim_pwm.c:21        #include "../app/app_board.h"
platform/adc_cur.c:4        #include "../src/drv/adc_cur.h"
platform/adc_vbus.c:4       #include "../src/drv/adc_vbus.h"
platform/app_board.c:4      #include "../src/app/app_board.h"
```

`app_board.h` looks like an application header but actually carries board parameters for the driver layer, forcing lower layers to include upper directories. `platform/*.c` including `../src/` is the same problem: the logical call still flows upward, but the **directory dependency points the opposite way**.

### 1.4 Application concepts leak into the BSP

```c
/* src/bsp/bsp_perf.h:14-18 */
typedef enum
{
    BSP_PERF_SIXSTEP = 0,      /* ← application-level control category */
    BSP_PERF_CURRENT,          /* ← application-level control category */
    BSP_PERF_CONTROL_COUNT
} bsp_perf_control_t;
```

`SIXSTEP`/`CURRENT` are application control categories. Adding an FOC speed loop would require editing the BSP enum — **the BSP is dragged along by application evolution**. `print_control()` in `bsp_perf.c:186` additionally hard-codes log formatting in the BSP.

### 1.5 Oversized files and a flat project tree

| File | Lines |
|---|---:|
| `src/app/app_sixstep.c` | 421 |
| `src/drv/tim_pwm.c` | 333 |
| `src/bsp/bsp_perf.c` | 230 |

`MDK-ARM/G474-FOC.uvprojx:550` put all 9 sources into one group `Application/Shared and Platform`, so the project tree did not reflect the layering.

---

## 2. Architecture principles

### 2.1 Four kinds of code, downward calls only

```text
src/app/*          Application: business state machines, strategy, protocol, tasks
   │            │
   │            └──────────────┐
   ▼                           ▼
src/algorithm/*           src/bsp/*           src/third_party/*
pure C, no hardware        changes with board     external libraries
```

- **Application** may call `algorithm`, `bsp` and `third_party`;
- **Algorithms** use standard C and their own header only; they must not include `app/`, `bsp/` or HAL. Hardware output goes through callbacks injected by the caller;
- **BSP** provides hardware primitives only and **must not contain `*_task()`**;
- **third_party** should depend only on standard C and never on app/bsp;
- **Test**: app/bsp headers inside `algorithm`/`third_party`, `*_task()` inside `bsp/`, or `#include "../app/` inside `bsp/`, are violations.

### 2.2 One folder per BSP module

```text
bsp/<module>/<module>.h
bsp/<module>/<module>_<aspect>.c
```

Folder names are lower_case_with_underscores and carry no `bsp_` prefix.

**Naming**: keep existing prefixes; only the misleading `app_board_*` becomes `bsp_board_*`. Everything else stays (`pwm_*`, `hall_*`, `adc_cur_*`, `adc_vbus_*`, `bsp_uart_*`, `bsp_cycle_*`). Stable APIs are not renamed for uniformity.

### 2.3 The dependency graph must be a DAG; zero lateral BSP calls

The target for lateral calls inside the BSP is **zero**. The only module others may depend on is `bsp/board/` (pure constants and time base, no state, no callbacks).

- `bsp/board/` depends on nothing else;
- every other module may include only **its own header** + `bsp/board/board.h`;
- `bsp/xxx.c` files must not include each other (pure `static inline` headers excepted, see 6.3);
- the algorithm modules (`algorithm/*`) must not include app/bsp headers or each other; third_party follows the same rule.

### 2.4 Split files, not functions

**File splitting and function splitting are different; only the latter costs runtime.**

| Action | Runtime cost |
|---|---|
| Move a function body into another `.c` | 0 (only loses cross-file inlining) |
| Split one logical block into extra calls "to keep files small" | 5–10 cycles per call (Cortex-M4, `BL` plus stacking) |

A normal call on a 170 MHz Cortex-M4 costs ~5–10 cycles ≈ 30–60 ns; a 20 kHz control period is 8500 cycles. **Splitting files is safe; splitting functions is governed by 6.2.**

---

## 3. Target layout

```text
src/
├── app/                              # Application: business, strategy, protocol, tasks
│   ├── app/                          # app_task: init, mode interlocks, main loop, shared state
│   │   ├── app.c/.h
│   │   └── app_state.c/.h            #   shared state (mode/timestamps/hall/ADC ready)
│   ├── cmd/                          # app_cmd_task: command read/parse
│   ├── control/                      # app_control_task: algorithm strategy (six-step now, FOC later)
│   ├── log/                          # app_log_task: formatting, queue, DMA pumping
│   ├── perf/                         # app_perf_task: accounting and reporting
│   └── status/                       # app_status_task: periodic status
├── algorithm/                        # Algorithms: standard C only, no hardware/application deps
│   ├── sixstep/                      #   step table and progression; hardware via callbacks
│   └── foc/                          #   reserved: Clarke/Park/PI/SVPWM
├── third_party/                      # External libraries (reserved)
└── bsp/                              # Board support layer
    ├── board/                        # constants and time base (others may depend on it)
    │   └── board.c/.h
    ├── pwm/                          # TIM1 six complementary outputs
    │   ├── pwm_io.c                  #   pure TIM1 primitives, never touches CTRL_SD
    │   ├── pwm_duty.c                #   duty clamp + write (hot path)
    │   ├── pwm_bridge.c              #   CTRL_SD interlock, self-check, emergency stop
    │   ├── pwm.c                     #   orchestration: init/start/stop
    │   └── pwm.h
    ├── hall/
    │   └── hall.c/.h
    ├── adc_cur/
    │   └── adc_cur.c/.h
    ├── adc_vbus/
    │   └── adc_vbus.c/.h
    ├── uart/
    │   ├── uart.c/.h                 #   format/enqueue, nonblocking TX/RX
    │   └── uart_queue.c              #   static ring buffer
    └── time/
        └── cycle.h                   #   DWT cycle counter, all static inline
```

Removed directories: `platform/` (merged into `src/bsp/`), `src/drv/` (merged into `src/bsp/`).

**Why algorithms live in `app/`**: six-step and FOC depend on `bsp/pwm`, `bsp/adc_cur`, `bsp/hall` and `bsp/time`, all below them. Placing them in `app/` yields only downward calls. In `bsp/` they would have to call those modules laterally, violating 2.3. Algorithms do not reference application business, so they can live beside it and evolve independently.

---

## 4. Module mapping

### 4.1 `bsp/` mapping

| Old file | Lines | New location | Split | Actual lines |
|---|---:|---|---|---:|
| `platform/app_board.c` | 24 | `bsp/board/board.c` | Move, rename to `bsp_board_*` | 56 |
| `src/drv/tim_pwm.c` | 333 | `bsp/pwm/` (four files) | See 4.3 | 42+66+84+200 |
| `src/drv/hall.c` | 94 | `bsp/hall/hall.c` | Move | 98 |
| `platform/adc_cur.c` | 66 | `bsp/adc_cur/adc_cur.c` | Move | 70 |
| `platform/adc_vbus.c` | 57 | `bsp/adc_vbus/adc_vbus.c` | Move | 61 |
| `src/bsp/bsp_uart.c` | 99 | `bsp/uart/uart.c` + `uart_queue.c` | Queue split out; TX moved to USART3_TX DMA | 110+100 |
| `src/bsp/bsp_perf.c` | 230 | `bsp/time/cycle.h` + `app/perf/` | See 4.4 | 62+205+67 |

### 4.2 `app/` mapping

| Old file/function | Lines | New location | Note |
|---|---:|---|---|
| `app_sixstep.c: app_init()` | ~70 | `app/app/app.c` | Init sequence |
| `app_sixstep.c: app_task()` | ~60 | `app/app/app.c` | Main loop |
| `app_sixstep.c: app_set_mode()` | ~110 | `app/app/app.c` | Mode machine + interlocks |
| `app_sixstep.c: apply_step()` + table + timer | ~130 | `app/control/app_control.c` (strategy) + `algorithm/sixstep/alg_sixstep.c` (pure algorithm) | Strategy calls algorithm; algorithm writes hardware via callbacks |
| `app_sixstep.c: handle_cmd()` | ~90 | `app/cmd/app_cmd.c` | Command parsing |
| `app_sixstep.c: print_status()` | ~40 | `app/status/app_status.c` | Status output |
| static variables in `app_sixstep.c` | — | `app/app/app_state.c` (shared) + `app/control/app_control.c` (six-step private) | See 4.5 |

`app_sixstep.h` / `app_sixstep.c` are retired. `Core/Src/main.c` still calls only `app_init()` / `app_task()`; **the calls are unchanged**, only the includes move (8.3).

### 4.3 Splitting `tim_pwm.c` (333 lines)

Split by **call frequency**, not by feature name:

| New file | Content | State | Call direction |
|---|---|---|---|
| `pwm_io.c` | `bsp_pwm_arm_tim()`, `bsp_pwm_disarm_tim()`, `bsp_pwm_enable_phase()` | stateless, **no `CTRL_SD`** | leaf |
| `pwm_duty.c` | `bsp_pwm_set_duty()`, `bsp_pwm_set_duty_raw()`, `pwm_write_ccr3()` (inline) | stateless | leaf |
| `pwm_bridge.c` | `bsp_pwm_enable_bridge()`, `bsp_pwm_enable_bridge_sixstep()`, `bsp_pwm_is_bridge_enabled()`, `bsp_pwm_check_ready()`, **`bsp_pwm_stop_emergency()`** | owns `s_bridge_enabled` shadow | leaf (`emergency_stop` writes registers directly) |
| `pwm.c` | `bsp_pwm_init()`, `bsp_pwm_start()`, `bsp_pwm_stop()` | stateless | **only cross-file orchestration point** |

`pwm_io.c` provides pure TIM1 primitives without `CTRL_SD`. The "stop outputs + open bridge" combination moved up to `pwm.c`; if `pwm_io` called `pwm_bridge` it would be a lateral BSP call, violating 2.3.

**`bsp_pwm_stop_emergency()` must stay in `pwm_bridge.c`**: it clears `s_bridge_enabled`, and only with the shadow state in the same file can "pull `CTRL_SD` low + clear `BDTR`/`CCER`/`CR1` + clear the shadow" stay in one function body. This is a safety requirement, not just performance.

**Hot-path check**: after the split, `bsp_pwm_set_duty()` and `bsp_pwm_enable_phase()` contain **zero cross-file calls**; cross-file calls exist only in `pwm.c` orchestration (cold path).

### 4.4 Splitting `bsp_perf.c` (230 lines): breaking the cycle

| New location | Content | Reason |
|---|---|---|
| `bsp/time/cycle.h` | `bsp_cycle_get_now()`, `bsp_cycle_get_hz()`, `bsp_cycle_convert_us()`, `bsp_cycle_init()`, **all `static inline`** | Hardware capability only; zero call overhead in ISRs |
| `app/perf/perf.h` | `app_perf_control_t { APP_PERF_SIXSTEP, APP_PERF_CURRENT }`, `app_perf_snapshot_t` | Business concepts belong to the application |
| `app/perf/perf.c` | `app_perf_init()`, `app_perf_begin_work()`, `app_perf_end_work()`, `app_perf_enter_irq()`, `app_perf_exit_irq()`, `app_perf_begin_control()`, `app_perf_end_control()`, `app_perf_get_snapshot()` | Accounting, no printing |
| `app/perf/perf_print.c` | `app_perf_task()`, `print_control()` | Printing is an application concern; calling `app_log_write()` is a same-layer call |

```text
app/perf ──→ bsp/time      (read cycle counter, downward) ✅
app/perf ──→ bsp/uart      (format output, downward) ✅
bsp/uart ──→ app/perf      ❌ gone, cycle broken
```

If `bsp/uart/uart.c` kept calling `app_perf_begin_work()`/`app_perf_end_work()`, it would depend on `app`, also violating 2.1. Therefore:

> **Time actual work branches in the application layer.** `app_log_task()` pairs `app_perf_begin_work()`/`app_perf_end_work()` around queue reclamation and DMA submission; `app_cmd_task()` times handling after receiving a character. `bsp/uart` contains only hardware primitives.

Empty receive/queue checks and DMA-busy checks fall into `poll`; queue progress and submission attempts fall into `work`. Peripheral DMA transfer duration is not CPU work. Actual work branches in the application pair the accounting entry/exit hooks.

**ISR hooks**: SysTick, DMA1_Channel2 and USART3 in `Core/Src/stm32g4xx_it.c` pair `app_perf_enter_irq()` / `app_perf_exit_irq()` in USER CODE entry/exit regions. `Core` hosts the application, so `Core → app` is a legal direction.

### 4.5 Shared application state

After the split, `s_mode`, `s_step`, `s_duty`, `s_elec_hz`, `s_adc_ready`, `s_last_step_ms` are needed by several files. Per `AGENTS.md` §4 ("no direct global variable access across modules"), they move into `app/app/app_state.c`; six-step private parameters (step, duty, electrical frequency) go to `app/control/app_control.c`:

```c
/* app/app/app_state.h */
app_mode_t app_get_mode(void);
void       app_set_mode(app_mode_t mode);
float      app_get_duty(void);
void       app_set_duty(float duty);
/* ...same for the remaining fields */
```

- Cold accessors are a few lines each; the call cost does not matter.
- The hot-path fields `step`/`duty` used by `app_apply_step()` are exposed as `static inline` accessors in `app_state.h` to avoid calls.
- Modules must not declare their own `extern` and touch variables directly.

Implementation note: a `static inline` accessor must see the field layout, so `app_state.h` defines `app_state_storage_t` and declares the storage object `app_state_storage` as `extern`. The convention is that only the inline accessors in that header and `app_state.c` touch it; every other module goes through `app_get_*`/`app_set_*`. This is the only way to have both zero call overhead and centralised ownership.

---

## 5. Dependency rules and automated checks

### 5.1 Allowed call matrix

| Caller ＼ Callee | `app/*` | `algorithm/*` | `third_party/*` | `bsp/*` | `Core` | HAL/CMSIS |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| `app/*` | same layer, allowed | ✅ | ✅ | ✅ | ❌ | forbidden |
| `algorithm/*` | ❌ | no mutual use | ❌ | ❌ | ❌ | ❌ |
| `third_party/*` | ❌ | ❌ | — | ❌ | ❌ | standard C only |
| `bsp/*` | ❌ | ❌ | ❌ | `board` only | ❌ | ✅ |
| `Core/*` | ✅ | ✅ | ✅ | ✅ | — | ✅ |

### 5.2 Three prohibitions

1. **No `#include "../app/` inside `bsp/`** (reverse dependency).
2. **No `.c`-to-`.c` includes between `bsp` modules**; only the module's own header and `bsp/board/board.h`.
3. **No application/algorithm identifiers inside `bsp/`**: `SIXSTEP`, `APP_MODE_`, `app_`, `alg_` prefixes.
4. **No `*_task()` inside `bsp/`**: main-loop progress belongs to the application.
5. **Algorithms must not depend on app/bsp**: `src/algorithm` and `src/third_party` must not include `app_*`, `bsp_*`, `pwm.h`, `hall.h`, `adc_*`, `uart.h`, `board.h`, `perf.h`, `main.h`.
6. **Every app subfolder must expose a `*_task()`** called from `app_task()`.

### 5.3 Check script

`tests/check_structure.sh` runs in Git Bash/CI and returns non-zero on violations. It differs from the original draft in two places:

- the business-identifier grep matches `SIXSTEP`, `APP_MODE_`, `app_perf`, `app_board`, `app_sixstep`, avoiding false positives on HAL macros such as `TIM_COUNTERMODE_xxx` / `TIM_OCMODE_xxx`;
- section banners must number **≥ 1** (the draft said ≥ 2): single-responsibility files legitimately contain only `write` or only `task`, and a hard minimum of 2 conflicts with "omit an absent section together with its banner".

Remaining checks: reverse/lateral BSP includes, stale paths, 250-line soft limit (warning only), and section order `init → task → read → write → hook`.

---

## 6. Split and code-structure details

### 6.1 250-line soft limit

- Soft limit of 250 lines per `.c`;
- Exceeding it is allowed but requires a "why not split" note in the file header;
- Typical exceptions: pure data tables, dense register-check sequences, hot-path single-file closures (6.2).

The current largest files are `bsp/pwm/pwm_bridge.c` (200) and `app/perf/perf.c` (205), both within the limit.

### 6.2 Three things that must not be split

1. **The `bsp_pwm_stop_emergency()` sequence**
   Pull `CTRL_SD` low → clear `BDTR` `MOE|AOE` → clear six `CCER` bits → clear `CR1.CEN` → `__DSB()`. Splitting creates a window where a higher-priority interrupt can run — a **safety red line**, not a performance concern.

2. **The future 50 µs current loop**
   `sample → Clarke → Park → PI → inverse Park → SVPWM → bsp_pwm_set_duty` must run sequentially in one function.

3. **Any read-check-write on a peripheral**
   E.g. "clamp + write CCR" in `bsp_pwm_set_duty()`. Splitting into `get_duty()` + `set_duty()` adds a peripheral read and loses atomicity.

### 6.3 `static inline` exemption

- **Pure computation** (clamping, conversion, sector lookup, cycles→µs, state accessors) may live in headers as `static inline` and be included across modules, **exempt from 5.2.2**;
- Preconditions: no global state beyond peripheral registers, no calls into other modules, no blocking;
- `bsp/time/cycle.h` is entirely of this kind (only reads `DWT->CYCCNT` and `SystemCoreClock`).

**Inline does not hurt testability**: `tests/stubs/perf_clock.h` already replaces CMSIS with macros (`#define DWT (&test_dwt)`, `#define __NOP()`, `#define __get_PRIMASK()`), so `static inline` expands against the stub structs on the host.

### 6.4 No LTO

**Decision: Link Time Optimization stays off.**

- The project selects ARM Compiler 5 (`uAC6 = 0` in `MDK-ARM/G474-FOC.uvprojx`); read its version from project metadata and check the installed environment;
- AC5 LTO is limited; real LTO means migrating to AC6 (`armclang -flto`), a separate project (syntax, startup file, many warnings);
- Hot-path performance is therefore guaranteed by **"single-file closure + `static inline`"**, the only verifiable means under AC5;
- If AC6 arrives later, 6.2 can be revisited.

`AGENTS.md` §3 forbids changing compiler options in `*.uvprojx`; `<Optim>`, `<Define>`, `<MiscControls>` are untouched. The only necessary project change is the **file list and include paths** (section 8).

### 6.5 Module template: init / task / read / write / hook

**Purpose**: more files are only useful if they are predictable. The template makes any module look the same — reviewers can immediately see whether a `write` section leaks side effects.

#### Section definitions

| Section | Meaning | Side effects | Context | May be absent |
|---|---|---|---|---|
| `init` | one-time setup | registers, clear state | boot, **bridge off** | yes (pure constants) |
| `task` | main-loop progress, nonblocking | same, but must return immediately | main loop | yes |
| `read` | pure query | **none**: no register writes, no static writes | any | yes |
| `write` | apply action | yes: registers, state, blocking waits | main loop/boot | yes |
| `hook` | ISR hooks, fault/emergency paths | yes, but nonblocking | interrupt/exception | yes |

Each module needs **at least one of `read`/`write`**; `init` is required except for pure constant modules (`bsp/board`).

**Declared `read` exception**: `app_perf_get_snapshot()` clears the window accumulators while returning the snapshot (read-and-clear). Clearing atomically with the snapshot is mandatory — two calls would let sampling slip in between and lose counts. It stays in `read` but **must be documented as read-and-clear** in the header and the implementation.

#### Function-to-section map

| Module | init | task | read | write | hook |
|---|---|---|---|---|---|
| `bsp/board` | — | — | `bsp_board_get_name` `bsp_board_get_millis` `bsp_board_get_pwm_arr` `bsp_board_get_deadtime_dtg` `bsp_board_get_boot_test` `bsp_board_get_drive_allowed` | `bsp_board_delay_ms` | — |
| `bsp/pwm` | `bsp_pwm_init` | — | `bsp_pwm_check_ready` `bsp_pwm_is_bridge_enabled` | `bsp_pwm_start` `bsp_pwm_stop` `bsp_pwm_set_duty` `bsp_pwm_set_duty_raw` `bsp_pwm_enable_phase` `bsp_pwm_enable_bridge` `bsp_pwm_enable_bridge_sixstep` | `bsp_pwm_stop_emergency` |
| `bsp/hall` | `bsp_hall_init` | — | `bsp_hall_read` `bsp_hall_get_sector_deg` `bsp_hall_get_state_str` | — | — |
| `bsp/adc_cur` | `bsp_adc_cur_init` | — | `bsp_adc_cur_read` | — | — |
| `bsp/adc_vbus` | `bsp_adc_vbus_init` | — | `bsp_adc_vbus_read` | — | — |
| `bsp/uart` | `bsp_uart_init` | — | `bsp_uart_read_byte` `bsp_uart_is_tx_busy` | `bsp_uart_write` | `HAL_UART_TxCpltCallback` |
| `app/log` | `app_log_init` | `app_log_task` | — | `app_log_write` | — |
| `bsp/time` | `bsp_cycle_init` | — | `bsp_cycle_get_now` `bsp_cycle_get_hz` `bsp_cycle_convert_us` | — | — |
| `app/perf` | `app_perf_init` | `app_perf_task` | `app_perf_get_snapshot` * | — | `app_perf_begin_work` `app_perf_end_work` `app_perf_begin_control` `app_perf_end_control` `app_perf_enter_irq` `app_perf_exit_irq` |
| `app/app` | `app_init` | `app_task` | `app_get_mode` and other shared-state reads | `app_set_mode`, `app_set_*` | — |
| `app/cmd` | — | `app_cmd_task` | — | — | — |
| `app/status` | — | `app_status_task` | — | — | — |
| `app/log` | `app_log_init` | `app_log_task` | — | `app_log_write` | — |
| `app/control` | `app_control_init` | `app_control_task` | `app_control_get_step/duty/elec_hz/phases` | `app_control_start` `app_control_set_duty` `app_control_set_elec_hz` | — |
| `algorithm/sixstep` | `alg_sixstep_init` | — | `alg_sixstep_get_phases` `alg_sixstep_next_step` | `alg_sixstep_apply` | — |

`*` read-and-clear exception, documented in the header.

#### Order inside a `.c`

```c
/** @file    pwm_duty.c
 *  @brief   duty clamping and write
 */
#include "pwm.h"

/* ---------------- private macros ---------------- */

/* ---------------- private types ---------------- */

/* ---------------- private variables ---------------- */

/* ---------------- internal ---------------- */
/* static helpers shared by several sections; single-section helpers stay in that section */

/* ---------------- init ---------------- */

/* ---------------- task ---------------- */

/* ---------------- read ---------------- */

/* ---------------- write ---------------- */

/* ---------------- hook ---------------- */
```

Rules:

- **Fixed order**; absent sections are omitted together with their banners;
- Private macros/types/variables come before `internal`;
- Multi-section `static` helpers go into `internal`; single-section helpers stay next to their caller;
- Banner format is `/* ---------------- <section> ---------------- */` (16 dashes) for the checker.

#### Order inside a `.h`

```c
/** @file    pwm.h
 *  @brief   TIM1 six complementary outputs
 */
#ifndef PWM_H
#define PWM_H
#include <stdint.h>

/* ---------------- public macros ---------------- */

/* ---------------- public types ---------------- */

/* ---------------- init ---------------- */

/* ---------------- task ---------------- */

/* ---------------- read ---------------- */

/* ---------------- write ---------------- */

#endif /* PWM_H */
```

Header order must match the `.c` sections — **the most valuable rule in the template**, because callers can see at a glance which functions are read-only, which have side effects, and which may run in an ISR.

#### Naming: module + verb + noun (mandatory since 2026-11-17)

Every non-`static` function shall be named `<module>_<verb>_<noun>`, reading naturally as English (singular nouns):

- **module**: `app`, `app_perf`, `bsp_board`, `bsp_pwm`, `bsp_hall`, `bsp_adc_cur`, `bsp_adc_vbus`, `bsp_uart`, `bsp_cycle` — the folder name without its parent;
- **verb**: the real action: `init`, `task`, `get`, `set`, `read`, `write`, `start`, `stop`, `reset`, `check`, `enable`, `apply`, `advance`, `convert`, `print`, `begin`, `end`, `enter`, `exit`, `delay`, `put`, `peek`, `consume`. Do not rewrite a natural `set`/`enable` into `write` just to fit a template;
- **noun**: the object, possibly compound (`duty_raw`, `queue_free`, `step_phases`);
- the noun may be omitted when the module itself is the object: `app_init`, `bsp_pwm_start`, `app_log_task`;
- examples: `bsp_pwm_set_duty`, `bsp_pwm_enable_bridge`, `app_get_mode`, `app_log_write`, `app_control_set_duty`, `alg_sixstep_next_step`;
- **exceptions**: HAL weak callbacks keep the library's fixed names (`HAL_UART_TxCpltCallback`); file-local `static` helpers only need `<verb>_<noun>` without the module prefix; type names (`app_state_storage_t`, `pwm_phase_t`) are out of scope;
- define the module prefix before adding a module; renaming an API updates `Core/`, `tests/` and both language docs in the same change.

#### Stage 3 alignment (done)

1. `hall.h` declarations reordered as `init` → `read`, matching `hall.c`;
2. `bsp/uart` keeps the natural `init / task / read / write` order and serves as the reference example;
3. `bsp/board` intentionally has only `read` and `write`; the file header documents this so nobody mistakes it for an omission.

---

## 7. Staged migration

Each stage ends with Keil `0 Error(s)` and a passing `python -X utf8 tests/run_native.py`. The implementation completed stages 0–4 in one pass, rebuilding and rerunning host regressions after each stage.

### Stage 0: create directories, move files

1. `git mv` sources to the section 3 locations (preserve history);
2. change `#include` paths only, **not a single function body**;
3. update `<FilePath>` entries in `MDK-ARM/G474-FOC.uvprojx` (8.1);
4. do not add/remove files in `MDK-ARM/`, `Core/`, `Drivers/`.

Acceptance: 0 errors; native tests pass; `grep -rn "platform/\|src/drv/" src/` is empty.

### Stage 1: break the dependency cycle

1. add `bsp/time/cycle.h` (all `static inline`);
2. add `app/perf/perf.c`, `perf_print.c`, `perf.h`; rename the enum to `APP_PERF_*`;
3. delete `bsp/perf/`;
4. rename the two ISR hooks in `Core/Src/stm32g4xx_it.c`;
5. drop `bsp_perf.h` from `bsp/uart/uart.c`; keep accounting in the application (currently in actual work branches of `app/log` and `app/cmd`, see 4.4);
6. update `tests/test_perf.c`.

Acceptance: the 5.3 script passes; no lateral includes inside `bsp/`.

### Stage 2: split `bsp/pwm/`

Split per 4.3 plus two opportunistic improvements:

1. factor `static inline pwm_write_ccr3(const float d[3])` out of `bsp_pwm_set_duty()` / `bsp_pwm_set_duty_raw()` (≈90% duplicated logic, only the lower clamp differs);
2. write `TIM1->CCR1/2/3` directly, dropping the `__HAL_TIM_SET_COMPARE` ternary cascade and the `s_pwm_ch[]` lookup.

Acceptance: `app_apply_step()` behaviour unchanged (step timing, floating phase, duty values bit-identical); zero cross-file calls inside `bsp_pwm_set_duty()`.

### Stage 3: split `app/`

Split per 4.2 and 4.5 into `app/app/`, `app/cmd/`, `app/status/`, with shared state centralised in `app_state.c`. Init, mode and main loop are merged into `app/app/app.c` (275 lines, above the 250-line soft limit; the merge rationale is in the file header); the step table and progression become the pure algorithm module `algorithm/sixstep/alg_sixstep.c`, while parameters and hardware callbacks live in `app/control`.

Acceptance: commands `0/1/2/h/+/-/>/<?` behave exactly as before; `Core/Src/main.c` calls unchanged.

### Stage 3.5: align to the template

Add section banners, reorder functions and sync `.h` declarations. **Pure rearrangement, no body changes.**

Acceptance: 5.3 item 6 passes; `git diff -w --ignore-blank-lines` shows moves only.

### Stage 4: docs and project groups

See section 8. Acceptance: `docs/README.md` index updated, both languages consistent, `AGENTS.md` §4 rewritten.

---

## 8. Related changes

### 8.1 `MDK-ARM/G474-FOC.uvprojx`

| Change | Content | Note |
|---|---|---|
| `<FilePath>` | New paths plus the split files | Done |
| `<GroupName>` | `Application/User/App`, `Application/User/Algo` and `Application/User/BSP` replace `Application/Shared and Platform` | Tree reflects layering |
| `<IncludePath>` | Add `../src/app/app;../src/app/cmd;../src/app/status;../src/app/perf;../src/app/log;../src/app/control;../src/algorithm/sixstep;../src/algorithm/foc;../src/third_party;../src/bsp/board;../src/bsp/pwm;../src/bsp/hall;../src/bsp/adc_cur;../src/bsp/adc_vbus;../src/bsp/uart;../src/bsp/time` | Allows `#include "pwm.h"` |
| **Forbidden** | Do not touch `<Optim>`, `<Define>`, `<MiscControls>`, `<uAC6>`, `<v6Lto>`, `<AdsLtoi>` | `AGENTS.md` §3 |

**USART3 TX DMA (added 2026-11-17)**: per `AGENTS.md` §8, the DMA configuration is changed only in the `.ioc` and regenerated by CubeMX; the driver never enables the DMA clock by hand. CubeMX adds `Core/Src/dma.c` and `Core/Inc/dma.h` (`MX_DMA_Init()` before `MX_USART3_UART_Init()`), generates `hdma_usart3_tx` (DMA1_Channel2, DMAMUX request `USART3_TX`) and the USART3 global interrupt in `HAL_UART_MspInit()`, and adds `DMA1_Channel2_IRQHandler()` / `USART3_IRQHandler()` to `stm32g4xx_it.c`. Both interrupts use preemption priority 15 (lowest) in the `.ioc`, matching the "UART lowest" rule. CubeMX keeps the user-added `Application/User/App`, `Application/User/Algo` and `Application/User/BSP` groups and the extra include paths.

`STM32-FOC.uvmpw` references only `MDK-ARM/G474-FOC.uvprojx` and needs no change.

### 8.2 `tests/`

| File | Change |
|---|---|
| `tests/test_perf.c` | Includes and symbols become `app_perf_*`; printing path covers `app/perf/perf_print.c` |
| `tests/test_runtime.c` | `app_board_*` stubs become `bsp_board_*`; includes switch to `pwm.h`/`uart.h`/`app.h`/`board.h` |
| `tests/test_g474.c` | Include paths updated; `app_board_*` → `bsp_board_*` |
| `tests/run_native.py` | Source lists and include paths updated |
| `tests/check_structure.sh` | New: structure compliance check (5.3) |

### 8.3 `Core/` USER CODE regions

| Location | Change |
|---|---|
| `Core/Src/main.c` | `../../src/app/app_sixstep.h` → `app.h`; `../../src/drv/tim_pwm.h` → `pwm.h` |
| `Core/Src/stm32g4xx_it.c` | `../../src/bsp/bsp_perf.h` → `perf.h`; `../../src/drv/tim_pwm.h` → `pwm.h` |
| `Core/Src/stm32g4xx_it.c` ISRs | `bsp_perf_irq_enter_ISR()` → `app_perf_enter_irq()`, same for exit |

The prefix-free includes rely on the `<IncludePath>` additions in 8.1. **No generated code outside USER CODE regions is modified.**

### 8.4 Documentation

| File | Change |
|---|---|
| `AGENTS.md` §4 | Rewritten to the section 3 layout plus the 2.1–2.4 principles and the 5.2 prohibitions |
| `README.md` / `README.zh-CN.md` | Repository layout updated |
| `docs/06-driver-modules.md` | Module list and paths updated |
| `docs/07-architecture-and-flow.md` | Module boundaries and dependency rules updated |
| `docs/10-bus-voltage-adc.md` | ADC4 implementation path updated |
| `docs/12-performance-monitoring.md` | `bsp_perf` → `app/perf` + `bsp/time` |
| `docs/README.md` | Index includes article 13 |
| `docs/04-cubemx-configuration.md` | USART3_TX DMA and interrupt priorities documented |

---

## 9. Acceptance criteria

- [x] Keil build `0 Error(s)`, `0 Warning(s)`
- [x] `python -X utf8 tests/run_native.py` passes
- [x] `bash tests/check_structure.sh` returns 0
- [x] `bsp/` has zero lateral includes, zero `../app/`, zero business identifiers
- [x] `bsp_pwm_set_duty()` / `bsp_pwm_enable_phase()` contain zero cross-file calls
- [x] The `bsp_pwm_stop_emergency()` sequence remains in a single function body
- [x] All modules follow the 6.5 template (banners, section order, header order)
- [x] `read` sections have no side effects; the only exception, `app_perf_get_snapshot`, is documented
- [x] Serial command behaviour matches the pre-restructure code (covered by `test_runtime.c`)
- [x] No compiler options changed in `*.uvprojx` beyond file list and include paths
- [x] `AGENTS.md` §4 matches the new layout
- [x] Both language versions updated, `docs/README.md` index updated
- [x] USART3 transmit uses DMA; the main loop no longer waits for TXE (RX remains polled)
- [x] **No new control behaviour; pins, clock, dead time, duty clamps and `CTRL_SD` timing unchanged**

## 10. Risks, rollback and status

| Risk | Impact | Mitigation |
|---|---|---|
| Hand-edited `uvprojx` | Project fails to open or loses files | Keep a copy before editing; open in Keil GUI after each stage (done: build verified) |
| `git mv` breaks history | Hard to trace | Always `git mv`, never delete+create; one commit per move |
| Stage 1 rename hits ISRs | Performance numbers lost | Read `[PERF]` after stage 1 (requires hardware) |
| Stage 3 changes timing | Commutation jitter | Compare `[PERF] SIXSTEP avg/max` before/after; revert if >10% (requires hardware) |
| Include path changes | Keil cannot find headers | Verified in the same batch as the file list |

**Rollback**: each stage is a separate commit; revert a failing stage with `git revert`. Nothing touches hardware configuration, so rollback carries no hardware risk.

### Implementation status

| Stage | Status |
|---|---|
| 0 Directories and file moves | Done 2026-11-17 |
| 1 Break dependency cycle | Done 2026-11-17 |
| 2 Split `bsp/pwm/` | Done 2026-11-17 |
| 3 Split `app/` | Done 2026-11-17 |
| 3.5 Align to template | Done 2026-11-17 |
| 4 Docs and project groups | Done 2026-11-17 |
| 5 USART3 TX DMA | Done 2026-11-17 (CubeMX generation + driver) |

### Known gaps

| Item | Reason | Next step |
|---|---|---|
| USART3 RX still polled | Command volume is tiny; RX DMA/IDLE would add NVIC and HAL callback complexity | Change only if command throughput becomes a bottleneck |
| DMA transmit on hardware | No hardware-in-the-loop here | Check for lost log bytes at 115200 and a sane `[PERF]` window; scope PB10 if needed |
| On-target performance comparison | No hardware-in-the-loop here | Compare `[PERF]` and commutation waveforms after power-up |
