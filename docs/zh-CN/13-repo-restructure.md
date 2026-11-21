# 13 仓库结构重构方案

[English](../en/13-repo-restructure.md) | **简体中文** | [文档目录](../README.md)

本文定义 `src/` 与 `platform/` 的目录重构方案：目标结构、依赖规则、拆分细则、分阶段迁移步骤与验收标准。方案已于 2026-11-17 实施完成，实施状态与已知偏差见第 10 节。

当前固件仍处于关桥诊断阶段，本次重构**不改变任何控制行为**：不改时钟、引脚、占空比钳位、死区、`CTRL_SD` 时序，也不引入电流环或 FOC。重构只动文件位置、文件粒度与头文件依赖。

---

## 1. 背景：现状的四类问题

### 1.1 `platform/` 与 `bsp/` 是概念重复的两个目录

`src/bsp/` 与 `platform/` 都存放"依赖具体芯片/板级"的实现，按 BSP（Board Support Package）通行定义属于同一层，却被拆成两个平级目录，且 `platform/` 位于仓库根目录、在 `src/` 之外：

| 文件 | 依赖 | 性质 |
|---|---|---|
| `src/bsp/bsp_uart.c` | `usart.h`、`huart3` | 板级实现 |
| `src/bsp/bsp_perf.c` | `main.h`、DWT、`HAL_GetTick` | 板级实现 |
| `platform/adc_cur.c` | `adc.h`、`hadc1~3` | 板级实现 |
| `platform/adc_vbus.c` | `adc.h`、`hadc4` | 板级实现 |
| `platform/app_board.c` | `main.h`、`HAL_GetTick` | 板级实现 |

后果：同一个模块被劈到两处。`src/drv/adc_cur.h` 声明接口，实现却在 `platform/adc_cur.c`；`src/drv/adc_vbus.h` 同理。

### 1.2 `bsp_uart` 与 `bsp_perf` 双向依赖，构成依赖环

```text
src/bsp/bsp_uart.c:6    #include "bsp_perf.h"
src/bsp/bsp_uart.c:42   bsp_perf_work_begin();
src/bsp/bsp_uart.c:45   bsp_perf_work_end();

src/bsp/bsp_perf.c:5    #include "bsp_uart.h"
src/bsp/bsp_perf.c:192  bsp_uart_printf("[PERF] ...");
src/bsp/bsp_perf.c:221  bsp_uart_printf("[PERF] CPU work=...");
```

这是**依赖环**，比"横向调用过多"更严重：两个模块无法独立编译、无法独立测试、任何一方接口变更都会波及另一方。

### 1.3 下层反向依赖上层的头文件

```text
src/drv/tim_pwm.c:21        #include "../app/app_board.h"
platform/adc_cur.c:4        #include "../src/drv/adc_cur.h"
platform/adc_vbus.c:4       #include "../src/drv/adc_vbus.h"
platform/app_board.c:4      #include "../src/app/app_board.h"
```

`app_board.h` 命名像应用层、实际是给驱动/平台层用的板级参数，导致下层必须 include 上层目录。`platform/*.c` 反向 include `../src/` 同理。调用方向虽然还是"向上取参数"，但**目录依赖方向与逻辑依赖方向相反**，是结构性的混乱。

### 1.4 BSP 层出现应用层业务概念

```c
/* src/bsp/bsp_perf.h:14-18 */
typedef enum
{
    BSP_PERF_SIXSTEP = 0,      /* ← 应用层的控制任务分类 */
    BSP_PERF_CURRENT,          /* ← 应用层的控制任务分类 */
    BSP_PERF_CONTROL_COUNT
} bsp_perf_control_t;
```

`SIXSTEP`/`CURRENT` 是应用侧的控制任务分类。将来增加 FOC 速度环时，必须回来修改 BSP 层的枚举——**BSP 被应用层的业务演进牵着走**。同文件 `bsp_perf.c:186` 的 `print_control()` 又把日志格式写死在 BSP 层。

### 1.5 单文件过大，工程分组不分层

| 文件 | 行数 |
|---|---:|
| `src/app/app_sixstep.c` | 421 |
| `src/drv/tim_pwm.c` | 333 |
| `src/bsp/bsp_perf.c` | 230 |

`MDK-ARM/G474-FOC.uvprojx:550` 把全部 9 个源文件塞进同一个组 `Application/Shared and Platform`，工程树无法反映分层。

---

## 2. 架构原则

### 2.1 四类代码，只允许由上向下调用

```text
src/app/*          应用层：业务状态机、控制策略、协议、任务（每个子文件夹都有 *_task）
   │            │
   │            └──────────────┐
   ▼                           ▼
src/algorithm/*           src/bsp/*           src/third_party/*
纯计算、无硬件依赖         换板/换引脚才改        外部库/协议栈
```

- **应用层**可以调用 `algorithm`、`bsp`、`third_party`；
- **算法层**只允许标准 C 与自己的头文件，不得 include `app/`、`bsp/` 或 HAL；
  需要写硬件时由调用方注入回调（依赖倒置）；
- **BSP** 只提供硬件原语（配置/读/写/查询），**不允许出现 `*_task()`**；
- **third_party** 尽量只依赖标准 C，不反向依赖 app/bsp；
- **判定标准**：`algorithm`/`third_party` 内出现 app/bsp 头文件、`bsp/` 内出现 `*_task()`、
  `bsp/` 内出现 `#include "../app/`，一律视为违规。

### 2.2 `bsp/` 按模块分子文件夹

每个模块一个文件夹，`.h` / `.c` 成对放置，头文件与实现同目录：

```text
bsp/<module>/<module>.h
bsp/<module>/<module>_<aspect>.c
```

模块文件夹名使用小写下划线，不带 `bsp_` 前缀（所在目录已表明层级）。

**函数命名规则**：统一为 `<模块>_<动词>_<名词>`，模块名与目录一致（`app`、`app_perf`、`sixstep`、`bsp_pwm` 等），详见 6.5 节。

### 2.3 依赖必须是 DAG，BSP 横向调用归零

BSP 内部横向调用目标为 **0 条**。唯一允许的被依赖模块是 `bsp/board/`（提供纯常量和时基，无状态、无回调）。

- `bsp/board/` 不依赖任何其他模块；
- 其余模块只允许 include **自己模块的头文件** + `bsp/board/board.h`；
- `bsp/xxx.c` 之间不得互相 include（`static inline` 纯计算头文件除外，见 6.3）；
- 算法层（`algorithm/*`）不得 include app/bsp 头文件，也不得互相 include；`third_party` 同理。

### 2.4 拆文件不拆函数

**文件拆分与函数拆分是两件事，只有后者影响运行时性能。**

| 动作 | 运行时开销 |
|---|---|
| 把一个 `.c` 拆成多个文件，函数体原样搬过去 | 0（只失去跨文件内联的可能） |
| 为"文件小"把一个完整逻辑拆成多次函数调用 | 5~10 周期/次（Cortex-M4，含 `BL` 与压栈） |

Cortex-M4 @170 MHz 下单次普通函数调用约 5~10 周期 ≈ 30~60 ns；20 kHz 控制周期为 8500 周期。**拆文件可以放心做；拆函数必须按 6.2 的红线判断。**

---

## 3. 目标目录结构

```text
src/
├── app/                              # 应用层：业务、策略、协议、任务
│   ├── app/                          # app_task：初始化、模式联锁、主循环、共享状态
│   │   ├── app.c/.h
│   │   └── app_state.c/.h            #   共享状态（模式/时间戳/霍尔/ADC 可用）
│   ├── cmd/                          # app_cmd_task：命令接收与解析
│   ├── control/                      # app_control_task：算法策略（六步参数，后续选 FOC）
│   ├── log/                          # app_log_task：日志格式化、队列、DMA 推进
│   ├── perf/                         # app_perf_task：性能统计与日志输出
│   └── status/                       # app_status_task：周期性状态打印
├── algorithm/                        # 算法层：只依赖标准 C，无硬件/应用依赖
│   ├── sixstep/                      #   换相表、步进规则；硬件输出经回调注入
│   └── foc/                          #   预留：Clarke/Park/PI/SVPWM
├── third_party/                      # 第三方源码（预留）
└── bsp/                              # 板级支持层
    ├── board/                        # 板级常量与时基（被其他模块依赖）
    │   └── board.c/.h
    ├── pwm/                          # TIM1 六路互补输出
    │   ├── pwm_io.c                  #   纯 TIM1 时序原语，不碰 CTRL_SD
    │   ├── pwm_duty.c                #   占空比钳位与写入（热路径）
    │   ├── pwm_bridge.c              #   CTRL_SD 联锁、自检、紧急关断
    │   ├── pwm.c                     #   编排：init/start/stop
    │   └── pwm.h
    ├── hall/
    │   └── hall.c/.h
    ├── adc_cur/
    │   └── adc_cur.c/.h
    ├── adc_vbus/
    │   └── adc_vbus.c/.h
    ├── uart/
    │   ├── uart.c/.h                 #   日志格式化入队、非阻塞收发
    │   └── uart_queue.c              #   静态环形队列
    └── time/
        └── cycle.h                   #   DWT 周期计数器，全部 static inline
```

被删除的目录：`platform/`（内容并入 `src/bsp/`）、`src/drv/`（内容并入 `src/bsp/`）。

**算法层落在 `app/` 的理由**：六步与 FOC 的依赖（`bsp/pwm`、`bsp/adc_cur`、`bsp/hall`、`bsp/time`）全部在下方，放进 `app/` 只产生向下调用，不引入任何 BSP 横向调用。若放进 `bsp/`，则算法模块必须横向调用 `bsp/pwm`、`bsp/adc_cur` 等，直接违反 2.3。算法只依赖 BSP、不引用应用业务，因此可以和 `app/` 业务并列维护、互不影响。

---

## 4. 模块映射表

### 4.1 `bsp/` 映射

| 现有文件 | 行数 | 新位置 | 拆分方式 | 实际行数 |
|---|---:|---|---|---:|
| `platform/app_board.c` | 24 | `bsp/board/board.c` | 整体搬迁，函数改名为 `bsp_board_*` | 56 |
| `src/drv/tim_pwm.c` | 333 | `bsp/pwm/` 四个文件 | 见 4.3 | 42+66+84+200 |
| `src/drv/hall.c` | 94 | `bsp/hall/hall.c` | 整体搬迁 | 98 |
| `platform/adc_cur.c` | 66 | `bsp/adc_cur/adc_cur.c` | 整体搬迁 | 70 |
| `platform/adc_vbus.c` | 57 | `bsp/adc_vbus/adc_vbus.c` | 整体搬迁 | 61 |
| `src/bsp/bsp_uart.c` | 99 | `bsp/uart/uart.c` + `uart_queue.c` | 队列拆出，发送改为 USART3_TX DMA | 110+100 |
| `src/bsp/bsp_perf.c` | 230 | `bsp/time/cycle.h` + `app/perf/` | 见 4.4 | 62+205+67 |

### 4.2 `app/` 映射

| 现有文件/函数 | 行数 | 新位置 | 说明 |
|---|---:|---|---|
| `app_sixstep.c: app_init()` | ~70 | `app/app/app.c` | 初始化序列 |
| `app_sixstep.c: app_task()` | ~60 | `app/app/app.c` | 主循环 |
| `app_sixstep.c: app_set_mode()` | ~110 | `app/app/app.c` | 模式状态机与联锁 |
| `app_sixstep.c: apply_step()` + 换相表 + 步进定时 | ~130 | `app/control/app_control.c`（策略）+ `algorithm/sixstep/alg_sixstep.c`（纯算法） | 策略调算法，算法经回调写硬件 |
| `app_sixstep.c: handle_cmd()` | ~90 | `app/cmd/app_cmd.c` | 命令解析 |
| `app_sixstep.c: print_status()` | ~40 | `app/status/app_status.c` | 状态打印 |
| `app_sixstep.c` 中的静态变量 | — | `app/app/app_state.c`（共享）+ `app/control/app_control.c`（六步私有） | 见 4.5 |

`app_sixstep.h` / `app_sixstep.c` 名称废弃；`Core/Src/main.c` 的 USER CODE 区仍只调用 `app_init()` / `app_task()`，**函数调用不变**，但它的 `#include` 路径需按 8.3 调整。

### 4.3 `tim_pwm.c`（333 行）的拆法

按**调用频次**分层，不按功能名词分层：

| 新文件 | 内容 | 状态 | 调用方向 |
|---|---|---|---|
| `pwm_io.c` | `bsp_pwm_arm_tim()`、`bsp_pwm_disarm_tim()`、`bsp_pwm_enable_phase()` | 无状态，**不碰 `CTRL_SD`** | 叶子 |
| `pwm_duty.c` | `bsp_pwm_set_duty()`、`bsp_pwm_set_duty_raw()`、`pwm_write_ccr3()`（inline） | 无状态 | 叶子 |
| `pwm_bridge.c` | `bsp_pwm_enable_bridge()`、`bsp_pwm_enable_bridge_sixstep()`、`bsp_pwm_is_bridge_enabled()`、`bsp_pwm_check_ready()`、**`bsp_pwm_stop_emergency()`** | 持有 `s_bridge_enabled` 影子态 | 叶子（`emergency_stop` 直写寄存器，不经 io 层） |
| `pwm.c` | `bsp_pwm_init()`、`bsp_pwm_start()`、`bsp_pwm_stop()` | 无状态 | **唯一跨文件编排点**：调 `pwm_io` 与 `pwm_bridge` |

`pwm_io.c` 只提供不含 `CTRL_SD` 操作的纯 TIM1 时序原语（`arm`/`disarm`/`phase_enable`）。原 `bsp_pwm_start()`/`bsp_pwm_stop()` 中"停输出 + 关桥"的组合动作上移到 `pwm.c` 编排层，因为 `pwm_io` 若直接调用 `pwm_bridge`，就成了 BSP 横向调用，违反 2.3。

**`bsp_pwm_stop_emergency()` 必须放在 `pwm_bridge.c`**，不能放 `pwm_io.c`：它最后要写 `s_bridge_enabled = 0u`，与该影子态同文件才能保证"拉低 `CTRL_SD` + 清 `BDTR`/`CCER`/`CR1` + 清影子态"三步不被拆散。这既是性能要求，更是安全要求。

**热路径验证**：拆分后 `bsp_pwm_set_duty()` 与 `bsp_pwm_enable_phase()` 内部只调用同文件 `static` 函数和 `static inline`，**零跨文件调用**。跨文件调用只出现在 `pwm.c` 的编排函数中（`bsp_pwm_start`/`bsp_pwm_stop`/`bsp_pwm_init`），这些都只在初始化与停机时执行，属冷路径。

### 4.4 `bsp_perf.c`（230 行）的拆法：破环

| 新位置 | 内容 | 理由 |
|---|---|---|
| `bsp/time/cycle.h` | `bsp_cycle_get_now()`、`bsp_cycle_get_hz()`、`bsp_cycle_convert_us()`、`bsp_cycle_init()`，**全部 `static inline`** | 只做 DWT 寄存器访问，属硬件能力；`static inline` 使 ISR 中零调用开销 |
| `app/perf/perf.h` | `app_perf_control_t { APP_PERF_SIXSTEP, APP_PERF_CURRENT }`、`app_perf_snapshot_t` | 业务概念归位到应用层 |
| `app/perf/perf.c` | `app_perf_init()`、`app_perf_begin_work()`、`app_perf_end_work()`、`app_perf_enter_irq()`、`app_perf_exit_irq()`、`app_perf_begin_control()`、`app_perf_end_control()`、`app_perf_get_snapshot()` | 统计逻辑，无打印 |
| `app/perf/perf_print.c` | `app_perf_task()`、`print_control()` | 打印策略归应用层，调用 `app_log_write()` 是同层调用 |

拆分后依赖关系：

```text
app/perf ──→ bsp/time      （读取周期计数，向下）✅
app/perf ──→ bsp/uart      （格式化输出，向下）✅
bsp/uart ──→ app/perf      ❌ 不存在，环已破除
```

`bsp/uart/uart.c` 中原本的 `bsp_perf_work_begin/end()` 改为 `app_perf_begin_work()`/`app_perf_end_work()` 后，`bsp/uart` 将**依赖 app 层**，这同样违反 2.1。因此改为：

> **计时留在应用层的实际工作分支**。`app_log_task()` 只对队列回收与 DMA 提交配对调用 `app_perf_begin_work()`/`app_perf_end_work()`；`app_cmd_task()` 只对收到字符后的处理计时。`bsp/uart` 只保留硬件原语。

空接收、空队列与 DMA 忙检查归入 `poll`，队列推进及提交尝试归入 `work`，外设 DMA 传输时间不计为 CPU 工作。计时入口/出口由应用层的实际工作分支配对调用。

**ISR 钩子**：`Core/Src/stm32g4xx_it.c` 的 SysTick、DMA1_Channel2 与 USART3 在 USER CODE 入口/出口配对调用 `app_perf_enter_irq()` / `app_perf_exit_irq()`。`Core/` 是应用的宿主，`Core → app` 属合法方向。

### 4.5 应用层共享状态的处理

拆分后 `s_mode`、`s_step`、`s_duty`、`s_elec_hz`、`s_adc_ready`、`s_last_step_ms` 等静态变量会被多个文件需要。按 `AGENTS.md` 第 4 节"不允许跨模块直接引用全局变量"，统一收敛到 `app/app/app_state.c`；六步私有参数（步号、占空比、电频率）归 `app/control/app_control.c`：

```c
/* app/app/app_state.h */
app_mode_t app_get_mode(void);
void       app_set_mode(app_mode_t mode);
float      app_get_duty(void);
void       app_set_duty(float duty);
/* ……其余字段同理 */
```

- 存取函数都是几行的冷路径，调用开销不影响性能；
- 热路径函数 `app_apply_step()` 所需的 `step`/`duty` 通过 `static inline` 访问器放在 `app_state.h`，避免函数调用；
- **不允许**模块直接 `extern` 声明并读写变量本体。

实现说明：`static inline` 访问器必须看到字段布局，因此 `app_state.h` 完整定义了 `app_state_storage_t` 并 `extern` 声明存储体 `app_state_storage`。约定该存储体只供本头文件的 inline 访问器与 `app_state.c` 使用，其他模块一律通过 `app_get_*`/`app_set_*` 接口访问；这是让"零调用开销"与"集中管理"同时成立的唯一做法。

---

## 5. 依赖规则与自动检查

### 5.1 允许的调用方向矩阵

| 调用方 ＼ 被调方 | `app/*` | `algorithm/*` | `third_party/*` | `bsp/*` | `Core` | HAL/CMSIS |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| `app/*` | 同层，允许 | ✅ | ✅ | ✅ | ❌ | 禁止直接使用 |
| `algorithm/*` | ❌ | 不互相依赖 | ❌ | ❌ | ❌ | ❌ |
| `third_party/*` | ❌ | ❌ | — | ❌ | ❌ | 仅标准 C |
| `bsp/*` | ❌ | ❌ | ❌ | 仅 `board` | ❌ | ✅ |
| `Core/*` | ✅ | ✅ | ✅ | ✅ | — | ✅ |

### 5.2 三条禁令

1. **`bsp/` 内禁止出现 `#include "../app/`**（反向依赖）。
2. **`bsp/` 内 `.c` 之间禁止互相 include**，只允许 include 自己模块的头文件与 `bsp/board/board.h`。
3. **`bsp/` 内禁止出现应用/算法标识符**：`SIXSTEP`、`APP_MODE_`、`app_`、`alg_` 前缀等。
4. **`bsp/` 内禁止出现 `*_task()`**：主循环推进属于应用层，BSP 只提供硬件原语。
5. **算法层禁止依赖 app/bsp**：`src/algorithm`、`src/third_party` 内不得 include `app_*`、`bsp_*`、`pwm.h`、`hall.h`、`adc_*`、`uart.h`、`board.h`、`perf.h`、`main.h`。
6. **app 每个子文件夹必须有 `*_task()`**，由 `app_task()` 依次调用。

### 5.3 检查脚本

脚本位于 `tests/check_structure.sh`，Git Bash / CI 可直接运行，返回非 0 表示存在违规。其中新增两项：

- `grep` 业务标识符时匹配 `SIXSTEP`、`APP_MODE_`、`app_perf`、`app_board`、`app_sixstep`，避免把 HAL 的 `TIM_COUNTERMODE_xxx`、`TIM_OCMODE_xxx` 误报为业务标识；
- 分节横幅要求 ≥ 1 个（原文为 ≥ 2）：存在只含 `write` 或只含 `task` 的单一职责文件，硬性要求 2 个会与"缺席的节连横幅一起省略"冲突。

其余检查项：`bsp` 反向依赖/横向 include、旧路径残留、单文件 250 行软限（仅提示）、分节顺序 `init → task → read → write → hook`。

---

## 6. 拆分与代码结构细则

### 6.1 单文件 250 行软限

- **软限**：单个 `.c` 尽量不超过 250 行；
- 超过时不强制拆分，但必须在文件头注释写明"为何不拆"；
- 允许超限的典型情形：纯数据表、密集的寄存器校验序列、热路径单文件闭环（见 6.2）。

当前最大文件为 `bsp/pwm/pwm_bridge.c`（200 行）与 `app/perf/perf.c`（205 行），均在软限内。

### 6.2 不许拆的三类函数

以下逻辑**必须保持在同一个函数体内**，不得为了减小文件或复用而拆成多次调用：

1. **`bsp_pwm_stop_emergency()` 的关桥序列**
   拉低 `CTRL_SD` → 清 `BDTR` 的 `MOE|AOE` → 清 `CCER` 六位 → 清 `CR1.CEN` → `__DSB()`。
   拆开会产生"中间可被更高优先级中断插入"的窗口，属于**安全红线**，不是性能问题。

2. **将来的 50 µs 电流环链路**
   `采样 → Clarke → Park → PI → 逆 Park → SVPWM → bsp_pwm_set_duty` 必须在同一函数内顺序执行，中间不得拆成"先算电流再算电压"。

3. **任何"读-判-写"外设的操作**
   例如 `bsp_pwm_set_duty()` 的"钳位 + 写 CCR"。拆成 `get_duty()` + `set_duty()` 会多一次外设读并丧失原子性。

### 6.3 `static inline` 豁免

- **纯计算**（钳位、换算、扇区查表、周期转 µs、状态存取器）允许以 `static inline` 写在头文件中，供多个模块横向引用，**不受 5.2 第 2 条限制**；
- 豁免条件：函数体内不得访问外设寄存器以外的全局状态、不得调用其他模块函数、不得阻塞；
- `bsp/time/cycle.h` 整体属于此类（仅读 `DWT->CYCCNT`、`SystemCoreClock`，无状态）。

**inline 不影响可测试性**：`tests/stubs/perf_clock.h` 已用宏桩（`#define DWT (&test_dwt)`、`#define __NOP()`、`#define __get_PRIMASK()`）替换 CMSIS，`static inline` 在主机测试中照常展开到桩结构体，无需额外改动。

### 6.4 不启用 LTO

**结论：重构后不启用 Link Time Optimization。**

依据：

- 工程选择 ARM Compiler 5（`MDK-ARM/G474-FOC.uvprojx` 的 `uAC6 = 0`）；具体版本从工程元数据读取并核对本机已安装环境；
- AC5 的 LTO 能力有限，不做跨模块激进内联；有效 LTO 需迁移 AC6（armclang `-flto`），属独立工程（语法、启动文件、大量警告需返工），不在本次范围；
- 因此热路径性能由 **"单文件闭环 + `static inline`"** 保证。该规则在 AC5 下是性能合规的**唯一**可验证手段；
- 若未来采纳 AC6，可重新评估是否放宽 6.2 的热路径单文件限制。

同时 `AGENTS.md` 第 3 节禁止修改 `*.uvprojx` 中的编译选项，本次不动 `<Optim>`、`<Define>`、`<MiscControls>` 等任何编译选项。唯一必要的工程变更是**文件列表与包含路径**，见第 8 节。

### 6.5 模块代码结构模板：init / task / read / write / hook

**目的**：拆分后文件变多，如果不统一结构，只是把混乱分散到了更多文件里。统一模板的价值是**打开任意模块，函数位置与顺序完全一致**——读代码不必先找，评审可以直接看"这个模块的 write 节有没有副作用泄漏"。

#### 五节定义

| 节 | 语义 | 允许的副作用 | 可调用上下文 | 允许缺席 |
|---|---|---|---|---|
| `init` | 一次性初始化 | 配置寄存器、清状态 | 启动序列，**关桥状态** | 是（纯常量模块） |
| `task` | 主循环推进，非阻塞 | 同上，但必须立即返回 | 主循环 | 是 |
| `read` | 纯查询 | **无**：不得写寄存器、不得改静态变量 | 任意 | 是 |
| `write` | 施加动作 | 有：写寄存器、改状态、阻塞等待 | 主循环/启动 | 是 |
| `hook` | ISR 打点、异常/紧急路径 | 有，但必须无阻塞 | **中断/异常上下文** | 是 |

约束：每个模块**至少要有 `read` 或 `write` 之一**；`init` 除纯常量模块（`bsp/board`）外必须提供。

**`read` 节的一个已声明例外**：`app_perf_get_snapshot()` 在返回快照的同时把窗口累加器清零。这是"取出即清零"（read-and-clear）语义，它为了快照与归零的原子性而必须如此——如果拆成"读快照 + 单独 clear"两次调用，中间会插入采样而丢失计数。此类函数仍放在 `read` 节，但**必须在头文件注释与实现处写明 read-and-clear**，不得静默隐藏副作用。

#### 现有函数的归节对照

| 模块 | init | task | read | write | hook |
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
| `app/app` | `app_init` | `app_task` | `app_get_mode` 等共享状态读取 | `app_set_mode`、`app_set_*` | — |
| `app/cmd` | — | `app_cmd_task` | — | — | — |
| `app/status` | — | `app_status_task` | — | — | — |
| `app/log` | `app_log_init` | `app_log_task` | — | `app_log_write` | — |
| `app/control` | `app_control_init` | `app_control_task` | `app_control_get_step/duty/elec_hz/phases` | `app_control_start` `app_control_set_duty` `app_control_set_elec_hz` | — |
| `algorithm/sixstep` | `alg_sixstep_init` | — | `alg_sixstep_get_phases` `alg_sixstep_next_step` | `alg_sixstep_apply` | — |

`*` 标记者为 read-and-clear 例外，需在注释中声明。

#### 文件内部顺序（`.c`）

```c
/** @file    pwm_duty.c
 *  @brief   占空比钳位与写入
 */
#include "pwm.h"

/* ---------------- 私有宏 ---------------- */

/* ---------------- 私有类型 ---------------- */

/* ---------------- 私有变量 ---------------- */

/* ---------------- internal ---------------- */
/* static 辅助函数；跨节共用的放这里，只服务单节的放在该节内 */

/* ---------------- init ---------------- */

/* ---------------- task ---------------- */

/* ---------------- read ---------------- */

/* ---------------- write ---------------- */

/* ---------------- hook ---------------- */
```

规则：

- **节顺序固定**，缺席的节连横幅一起省略，不留空节；
- 私有的宏/类型/变量一律在 `internal` 之前；
- `static` 辅助函数若被多节使用则放 `internal`，只服务单节则放在该节内紧邻其调用者；
- 横幅格式统一为 `/* ---------------- <节名> ---------------- */`（16 个减号），便于脚本检查与目视定位。

#### 文件内部顺序（`.h`）

```c
/** @file    pwm.h
 *  @brief   TIM1 六路互补 PWM
 */
#ifndef PWM_H
#define PWM_H
#include <stdint.h>

/* ---------------- 公共宏 ---------------- */

/* ---------------- 公共类型 ---------------- */

/* ---------------- init ---------------- */

/* ---------------- task ---------------- */

/* ---------------- read ---------------- */

/* ---------------- write ---------------- */

#endif /* PWM_H */
```

头文件声明顺序必须与 `.c` 的分节顺序一致——**这是模板最有价值的一条**，因为调用方读头文件就能知道哪些函数只读、哪些有副作用、哪些能在 ISR 里调。

#### 命名规则：模块 + 动词 + 名词（2026-11-17 起强制）

所有非 `static` 函数必须写成 `<模块>_<动词>_<名词>`（三者在英文里自然连读，可数名词用单数）：

- **模块**：`app`、`app_perf`、`bsp_board`、`bsp_pwm`、`bsp_hall`、`bsp_adc_cur`、`bsp_adc_vbus`、`bsp_uart`、`bsp_cycle`，与目录名一致、去掉层级；
- **动词**：`init`、`task`、`get`、`set`、`read`、`write`、`start`、`stop`、`reset`、`check`、`enable`、`apply`、`advance`、`convert`、`print`、`begin`、`end`、`enter`、`exit`、`delay`、`put`、`peek`、`consume` 等真实动作，不为凑模板把 `set`/`enable` 改写成 `write`；
- **名词**：操作对象，可用复合词（`duty_raw`、`queue_free`、`step_phases`）；
- 初始化/调度整个模块时名词可省略：`app_init`、`bsp_pwm_start`、`app_log_task`；
- 例：`bsp_pwm_set_duty`、`bsp_pwm_enable_bridge`、`app_get_mode`、`app_log_write`、`app_control_set_duty`、`alg_sixstep_next_step`；
- **例外**：HAL 弱回调沿用库的固定名字（`HAL_UART_TxCpltCallback`）；文件内 `static` 辅助函数只要求 `<动词>_<名词>`，不重复模块前缀；类型名（`app_state_storage_t`、`pwm_phase_t`）不在本规则内；
- 新增模块先确定模块前缀，再写函数名；重命名 API 时同步 `Core/`、`tests/`、中英文档。

#### 阶段 3 顺带对齐的三处（已实施）

1. `hall.h` 声明顺序按模板重排为 `init` → `read`，与 `hall.c` 实现顺序一致；
2. `bsp/uart` 的四个函数保持 `init / task / read / write` 顺序，作为模板的参考范例；
3. `bsp/board` 只有 `read` 与 `write` 两节，是**有意为之**（无状态、无初始化），文件头注释已写明，避免后人误以为是遗漏。

---

## 7. 分阶段迁移步骤

每个阶段结束时必须满足：Keil 编译 `0 Error(s)`，且 `python -X utf8 tests/run_native.py` 全部通过。任一阶段不通过不得进入下一阶段。本次实施按阶段 0→4 连续完成，每阶段均重新编译并跑通主机回归。

### 阶段 0：建立目录、搬迁文件（不改任何函数体）

1. `git mv` 搬迁源文件到第 3 节目标位置（保留文件历史）；
2. 仅修改 `#include` 路径与文件内相对路径，**一行函数体都不动**；
3. 更新 `MDK-ARM/G474-FOC.uvprojx` 的 `<FilePath>`（见 8.1）；
4. `MDK-ARM/`、`Core/`、`Drivers/` 不加文件、不删文件。

验收：编译 0 error；native 测试全过；`grep -rn "platform/\|src/drv/" src/` 无输出。

### 阶段 1：破除依赖环

1. 新建 `bsp/time/cycle.h`（全部 `static inline`）；
2. 新建 `app/perf/perf.c`、`perf_print.c`、`perf.h`，从 `bsp_perf.c` 搬入统计与打印逻辑，枚举改名为 `APP_PERF_*`；
3. 删除 `bsp/perf/` 目录；
4. `Core/Src/stm32g4xx_it.c` 两处 ISR 钩子改名为 `app_perf_enter_irq()`/`app_perf_exit_irq()`；
5. `bsp/uart/uart.c` 移除 `bsp_perf.h` 依赖；计时留在应用层（当前由 `app/log` 与 `app/cmd` 的实际工作分支负责，见 4.4）；
6. `tests/test_perf.c` 同步改名与 include。

验收：`sh` 运行 5.3 脚本通过；`bsp/` 内无任何横向 include。

### 阶段 2：拆分 `bsp/pwm/`

按 4.3 拆成 `pwm_io.c` / `pwm_duty.c` / `pwm_bridge.c` / `pwm.c`，并完成两项顺带优化：

1. `bsp_pwm_set_duty()` 与 `bsp_pwm_set_duty_raw()` 抽出公共的 `static inline pwm_write_ccr3(const float d[3])`（两者原本逻辑重复约 90%，仅钳位下限不同）；
2. 热路径改直写 `TIM1->CCR1/2/3`，去掉 `__HAL_TIM_SET_COMPARE` 的三元级联与 `s_pwm_ch[]` 查表。

验收：`apply_step()` 行为不变（换相时序、悬空相、占空比数值逐位一致）；`bsp_pwm_set_duty()` 内跨文件调用为 0。

### 阶段 3：拆分 `app/`

按 4.2 与 4.5 拆成 `app/app/`、`app/cmd/`、`app/status/`，共享状态收敛到 `app_state.c`。初始化、模式与主循环合并在 `app/app/app.c`（275 行，超过 250 行软限，文件头已注明合并理由）；换相表与步进规则独立为 `algorithm/sixstep/alg_sixstep.c`（纯算法），参数与硬件回调在 `app/control`。

验收：串口命令 `0/1/2/h/+/-/>/<?` 行为与拆分前完全一致（逐条比对日志输出）；`Core/Src/main.c` 调用不变。

### 阶段 3.5：对齐结构模板

按 6.5 给全部模块补分节横幅、重排函数顺序、同步 `.h` 声明顺序。这是**纯重排，不改变任何函数体**。

验收：5.3 脚本第 6 项通过；`git diff --stat` 显示改动仅限行移动，函数体无变更（可用 `git diff -w --ignore-blank-lines` 抽查）。

### 阶段 4：同步文档与工程分组

见第 8 节。验收：`docs/README.md` 索引更新、中英文档一致、`AGENTS.md` 第 4 节改写完成。

---

## 8. 连带改动清单

### 8.1 `MDK-ARM/G474-FOC.uvprojx`

| 变更 | 内容 | 说明 |
|---|---|---|
| 文件列表 `<FilePath>` | 旧路径改为新路径 + 拆分产生的新文件 | 已完成 |
| 文件分组 `<GroupName>` | 拆成 `Application/User/App`、`Application/User/Algo`、`Application/User/BSP` 三组，取代 `Application/Shared and Platform` | 让工程树反映分层 |
| 包含路径 `<IncludePath>` | 追加 `../src/app/app;../src/app/cmd;../src/app/status;../src/app/perf;../src/app/log;../src/app/control;../src/algorithm/sixstep;../src/algorithm/foc;../src/third_party;../src/bsp/board;../src/bsp/pwm;../src/bsp/hall;../src/bsp/adc_cur;../src/bsp/adc_vbus;../src/bsp/uart;../src/bsp/time` | 使源码可写 `#include "pwm.h"` 而非多层 `../` |
| **禁止** | 不改 `<Optim>`、`<Define>`、`<MiscControls>`、`<uAC6>`、`<v6Lto>`、`<AdsLtoi>` | `AGENTS.md` 第 3 节 |

`<IncludePath>` 的追加属于结构变更的必要条件（否则多层相对路径不可维护），不是编译优化选项。

**USART3 TX DMA（2026-11-17 追加）**：按 `AGENTS.md` 第 8 节，DMA 配置只在 `.ioc` 中修改并由 CubeMX 重新生成，驱动不手工使能 DMA 时钟。CubeMX 新增 `Core/Src/dma.c`、`Core/Inc/dma.h`（`MX_DMA_Init()`，置于 `MX_USART3_UART_Init()` 之前），并在 `Core/Src/usart.c` 的 `HAL_UART_MspInit()` 中生成 `hdma_usart3_tx`（DMA1_Channel2、DMAMUX 请求 `USART3_TX`）与 USART3 全局中断；`Core/Src/stm32g4xx_it.c` 增加 `DMA1_Channel2_IRQHandler()` 与 `USART3_IRQHandler()`。两个中断的抢占优先级在 `.ioc` 中设为 15（最低），符合第 6 节"串口优先级最低"。CubeMX 会保留工程里用户自行添加的 `Application/User/App`、`Application/User/Algo`、`Application/User/BSP` 分组与追加的包含路径。

`STM32-FOC.uvmpw` 只引用 `MDK-ARM/G474-FOC.uvprojx`，不随文件增删而变化，无需修改。

### 8.2 `tests/`

| 文件 | 变更 |
|---|---|
| `tests/test_perf.c` | include 与符号名改为 `app_perf_*`；打印路径覆盖 `app/perf/perf_print.c` |
| `tests/test_runtime.c` | `app_board_*` 桩改名为 `bsp_board_*`；include 换为 `pwm.h`/`uart.h`/`app.h`/`board.h` |
| `tests/test_g474.c` | include 路径更新；`app_board_*` → `bsp_board_*` |
| `tests/run_native.py` | 源文件清单与 include 路径更新 |
| `tests/check_structure.sh` | 新增：结构合规检查（第 5.3 节） |

### 8.3 `Core/` USER CODE 区

| 位置 | 变更 |
|---|---|
| `Core/Src/main.c` | `../../src/app/app_sixstep.h` → `app.h`；`../../src/drv/tim_pwm.h` → `pwm.h` |
| `Core/Src/stm32g4xx_it.c` | `../../src/bsp/bsp_perf.h` → `perf.h`；`../../src/drv/tim_pwm.h` → `pwm.h` |
| `Core/Src/stm32g4xx_it.c` ISR | `bsp_perf_irq_enter_ISR()` → `app_perf_enter_irq()`，`bsp_perf_irq_exit_ISR()` → `app_perf_exit_irq()` |

`Core/` 中的 `#include` 改为无前缀形式，依赖 8.1 的 `<IncludePath>` 追加。**`Core` 之外的生成代码一律不动**，只改 `USER CODE` 区。

### 8.4 文档

| 文件 | 变更 |
|---|---|
| `AGENTS.md` 第 4 节 | 按第 3 节重写目录结构，并补入 2.1~2.4 的架构原则与 5.2 的三条禁令 |
| `README.md` / `README.zh-CN.md` | 更新仓库结构描述 |
| `docs/06-driver-modules.md` | 模块列表与路径更新 |
| `docs/07-architecture-and-flow.md` | 第 1 节模块边界与依赖规则更新 |
| `docs/10-bus-voltage-adc.md` | ADC4 实现路径更新 |
| `docs/12-performance-monitoring.md` | `bsp_perf` → `app/perf` + `bsp/time` 机制说明 |
| `docs/README.md` | 索引加入第 13 篇 |
| `docs/04-cubemx-configuration.md` | 补充 USART3_TX DMA 与中断优先级配置 |

---

## 9. 验收标准

- [x] Keil 编译 `0 Error(s)`，`0 Warning(s)`
- [x] `python -X utf8 tests/run_native.py` 全部通过
- [x] `bash tests/check_structure.sh` 返回 0
- [x] `bsp/` 内零横向 include、零反向 `../app/`、零业务标识符
- [x] `bsp_pwm_set_duty()` / `bsp_pwm_enable_phase()` 内跨文件调用为 0
- [x] `bsp_pwm_stop_emergency()` 关桥序列仍在单一函数体内
- [x] 全部模块符合 6.5 结构模板：分节横幅齐全、`init → task → read → write → hook` 顺序正确、`.h` 声明顺序与 `.c` 一致
- [x] `read` 节函数经抽查确认无副作用；唯一例外 `app_perf_get_snapshot` 的 read-and-clear 已在注释中声明
- [x] 串口命令行为与重构前一致（主机回归 `test_runtime.c` 逐条覆盖）
- [x] 除 8.1 列出的文件列表与包含路径外，`*.uvprojx` 的编译选项无变更
- [x] `AGENTS.md` 第 4 节已改写，且与新目录一致
- [x] 中英文档同步完成，`docs/README.md` 索引已更新
- [x] USART3 发送改为 DMA，主循环不再等待 TXE；接收仍为主循环轮询
- [x] **未新增任何控制行为、未改动引脚、时钟、死区、占空比钳位、`CTRL_SD` 时序**

## 10. 风险与回退

| 风险 | 影响 | 应对 |
|---|---|---|
| `uvprojx` 手工编辑出错 | 工程打不开或文件丢失 | 改动前对 `MDK-ARM/G474-FOC.uvprojx` 做一份副本；每阶段用 Keil GUI 打开确认文件树完整 |
| `git mv` 后历史断链 | 难以追溯变更 | 统一用 `git mv` 而非"删除+新建"；单次提交只做搬迁 |
| 阶段 1 改名波及 ISR | 性能统计丢失 | 阶段 1 完成后读一次 `[PERF]` 输出，确认 `work/busy/irq` 数值合理（需实板） |
| 阶段 3 拆分改变时序 | 换相周期抖动 | 每电步前后各读一次 `[PERF] SIXSTEP avg/max`，与重构前对比，偏差 >10% 则回退（需实板） |
| 包含路径变更导致 Keil 找不到头文件 | 编译失败 | 与文件列表同批验证（已完成） |

**回退方式**：本次重构按阶段独立提交，任一阶段验收不通过即 `git revert` 该阶段提交，回到上一可编译状态。全部变更不涉及硬件配置，回退无硬件风险。

### 实施状态

| 阶段 | 状态 |
|---|---|
| 0 建立目录、搬迁文件 | 已完成 2026-11-17 |
| 1 破除依赖环 | 已完成 2026-11-17 |
| 2 拆分 `bsp/pwm/` | 已完成 2026-11-17 |
| 3 拆分 `app/` | 已完成 2026-11-17 |
| 3.5 对齐结构模板 | 已完成 2026-11-17 |
| 4 同步文档与工程分组 | 已完成 2026-11-17 |
| 5 USART3 发送 DMA 化 | 已完成 2026-11-17（CubeMX 生成 + 驱动改造） |

### 已知未完成项

| 项 | 原因 | 后续动作 |
|---|---|---|
| USART3 接收仍为主循环轮询 | 命令量小，轮询单字节足够；改 RX DMA/IDLE 会增加 NVIC 与 HAL 回调复杂度 | 若命令吞吐成为瓶颈再改 |
| DMA 发送的实板验证 | 无硬件在环条件 | 上电后确认 115200 下日志无丢字、`[PERF]` 窗口正常；必要时用逻辑分析仪看 PB10 波形 |
| 实板性能对比 | 无硬件在环条件 | 上电后对比 `[PERF]` 与换相波形，确认无退化 |
