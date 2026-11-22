# AGENTS.md — 工程规则

本文件是**权威约定**。任何人（含 AI 助手）在本仓库改动代码前必须先读本文件。
与本文件冲突的代码一律视为错误。

---

## 1. 项目上下文

STM32G474RET6 / DevEBox Ver.20 + ATK-PD6010B + BL57H95，裸机霍尔 FOC 工程，不引入 MCSDK。
本文件只规定开发约束，不复制当前固件状态、引脚表、参数表或测试记录。

## 2. 文档归属与修改范围

[文档目录](docs/README.md) 定义每个主题的唯一维护入口。修改前先定位主题，通常只更新对应中英文两份。

- 引脚/接线 → `01-hardware-and-wiring.md`；物理排针/板载占用 → `11-g474-board-guide.md`。
- 生成配置 → `.ioc` 为源，必要说明只放 `04-cubemx-configuration.md`。
- 模块接口/时序 → `06-driver-modules.md`；母线/VREF → `10-bus-voltage-adc.md`；性能统计 → `12-performance-monitoring.md`。
- 启动许可、命令、默认值、操作限制、构建与验证方法 → `09-validation-and-maintenance.md`。
- 应用编排/模块边界 → `07-architecture-and-flow.md`；理论/后续设计 → 02、03、05。
- README 只做入口；不要因内部行为或参数变化同步改 README、AGENTS 和所有模块文档。
- 只有维护规则变化才改 AGENTS；只有导航变化才改文档目录。
- 排查过程、审查意见、临时诊断日志直接在会话反馈；按用户要求记录 ITO，不新增到 docs。
- 不为每个 `.c/.h` 新建文章，不复制完整源码接口清单；多个职责变化时才更新多个归属主题，并说明原因。

---

## 3. 禁区（绝对禁止修改）

| 路径 | 原因 |
|---|---|
| `datasheet/**` | 器件资料，只读 |
| `Drivers/`、`Middlewares/` | CubeMX 生成的 HAL / CMSIS，改动会被覆盖 |
| `Core/**` 中 `USER CODE BEGIN/END` 之外的部分 | 同上 |
| `MDK-ARM/startup_*.s`、`*.uvprojx` 中的编译选项 | 会破坏工具链一致性 |

需要改外设配置时：改 `.ioc` → 重新生成；或在 `USER CODE` 区内写用户代码。

---

## 4. 目录结构

四类代码，只允许由上向下依赖：应用业务 → 算法/第三方；应用业务 → BSP；算法层不得依赖 app 或 bsp。

```
src/
├── app/                              # 应用层：业务状态机、控制策略、协议、任务
│   ├── app/                          # app_task / app_set_mode / 共享状态（app_state）
│   ├── cmd/                          # app_cmd_task：串口命令接收与解析
│   ├── control/                      # app_control_task：算法策略（六步参数，后续选 FOC）
│   ├── log/                          # app_log_task：日志格式化、静态队列、DMA 推进
│   ├── perf/                         # app_perf_task：性能统计与日志输出
│   └── status/                       # app_status_task：周期性状态打印
├── algorithm/                        # 算法层：无任何硬件/应用依赖（只用标准 C）
│   ├── sixstep/                      # 换相表、步进规则；输出经调用方注入的回调
│   └── foc/                          # 预留：Clarke/Park/PI/SVPWM
├── bsp/                              # 板级层：换板/换引脚必须改的代码，禁止出现 task
│   ├── board/                        # 板级常量与时基（唯一允许被其他 BSP 依赖）
│   ├── pwm/                          # pwm_io / pwm_duty / pwm_bridge / pwm 编排
│   ├── hall/                         # 霍尔读取
│   ├── adc_cur/                      # 三相电流 ADC
│   ├── adc_vbus/                     # 母线电压 ADC4
│   ├── uart/                         # USART3 + TX DMA 硬件接口（无队列、无 task）
│   └── time/                         # cycle.h：DWT 周期计数（全部 static inline）
└── third_party/                      # 第三方源码：尽量只依赖标准 C，不反向依赖 app/bsp
```

规则：

1. **依赖方向**：`app → algorithm`、`app → bsp`、`app → third_party`；
   `algorithm` 与 `third_party` 不得 include `app/`、`bsp/` 或厂商 HAL；
   `bsp/` 不得 include `app/` 或 `algorithm/`。
2. **BSP 不得有任务**：`bsp/` 内不允许出现 `*_task()`；主循环推进一律放 `app/`。
   BSP 只提供"配置、读、写、查询"这类硬件原语。
3. **BSP 横向调用归零**：`bsp/` 内 `.c` 之间不得互相 include，只允许 include
   自己模块的头文件与 `bsp/board/board.h`；纯计算 `static inline` 头文件例外。
4. **BSP 不得出现应用/算法业务标识符**：`SIXSTEP`、`APP_MODE_`、`app_`、`alg_` 前缀等。
5. **判定标准**："换一块板子或换一个引脚必须改的代码"放 `bsp/`；"换产品需求才改的代码"放 `app/`；
   纯数学/纯决策放 `algorithm/`。
6. **app 每个子文件夹都必须有 `*_task()` 入口**，由 `app_task()` 依次调用。
7. 每个 `.c` 只负责一个功能域；跨模块只通过头文件里的公开函数通信，
   **不允许跨模块直接引用全局变量**；共享状态集中在 `app/app/app_state.c`，
   六步参数在 `app/control`，算法层无状态（状态由调用方传入）。
8. 模块文件按 `init → task → read → write → hook` 分节，头文件声明顺序与 `.c` 一致；
   单个 `.c` 软限 250 行，超限须在文件头写明"为何不拆"。
9. **函数命名：`<模块>_<动词>_<名词>`（强制）**。模块名与目录一致（`app`、`app_cmd`、
   `app_control`、`app_log`、`app_perf`、`app_status`、`alg_sixstep`、`bsp_pwm` 等）；
   动词用真实动作，不把 `set`/`enable` 硬改成 `write`。
   例：`bsp_pwm_set_duty`、`app_control_set_duty`、`app_log_write`、`alg_sixstep_next_step`。
   例外：HAL 弱回调沿用库名；文件内 `static` 辅助函数不重复模块前缀；类型名不受约束。
10. 合规自检：`bash tests/check_structure.sh` 必须返回 0。

唯一工程位于 `MDK-ARM/G474-FOC.uvprojx`，根目录 `STM32-FOC.uvmpw` 只引用该目标；
工程分组为 `Application/User/App`、`Application/User/Algo`、`Application/User/BSP`，
包含路径与上表目录一一对应。`CTRL_SD`、`bsp_pwm_stop_emergency` 的关桥时序、
ADC 句柄封装仍由 `bsp/` 负责，应用不直接访问 HAL ADC 句柄。
`Core/Drivers/MDK-ARM` 适用生成区域与 SDK 保护规则。

---

## 5. 代码规范（MISRA C 子集 + 本项目约定）

### 必须

- **禁止 `malloc` / `free`**，所有缓冲静态分配；
- 使用固定宽度类型：`uint8_t / int16_t / uint32_t / float`，禁止裸 `int`、`long`；
- 常量对象加 `const`；宏参数加括号 `#define FOO(x) ((x) * 2)`；
- `switch` 必须有 `default`；
- 禁止在条件里做赋值：先赋值再判断；
- **每个函数定义前必须有头部注释**（`@brief` / `@param` / `@retval`）；
- 关键逻辑写**为什么**（WHY），不要重复代码在做什么（WHAT）；
- 中断服务函数命名 `xxx_ISR()`，且必须短小；耗时计算放主循环。

### 禁止

- 注释掉的死代码、未使用的变量、`#if 0` 残块；
- 在 ISR 里调用 `printf`、浮点除法、查大表、延时；
- 魔法数字：引脚、限幅、阈值、换算系数必须定义成具名常量或宏；
- 直接操作寄存器绕过已封装的驱动层（除非该驱动就是本模块）。

### 示例

```c
/**
 * @brief  将 ADC 原始值换算为相电流
 * @param  raw: 12 位 ADC 原始值（0~4095）
 * @retval 相电流，单位 A，范围约 -10.4 ~ +17.1
 */
static float adc_to_current(uint16_t raw)
{
    // 采样链路：20mΩ × 增益6 + 1.25V 偏置，故 1A 对应 0.12V
    float volt = (float)raw * ADC_VREF / ADC_FULL_SCALE;
    return (volt - CURRENT_OFFSET_V) / CURRENT_GAIN_V_PER_A;
}
```

---

## 6. 实时性红线

| 规则 | 原因 |
|---|---|
| 电流环 ISR 必须在下一次更新事件前结束（< 50 µs，目标 < 15 µs） | 否则占空比更新延迟一拍 |
| ISR 内不做阻塞等待（`HAL_Delay`、等标志位死循环） | 会拖垮整个控制环 |
| 中断优先级：ADC 注入 JEOC > 霍尔 EXTI > 速度环 > 串口 | 电流环最不能被推迟 |
| 电流环与速度环之间用**无锁单写单读**变量传递，不加临界区 | 避免关中断影响采样 |
| float 运算集中在 ISR，其他模块可用 double | M4 的 FPU 是单精度 |

---

## 7. 安全红线（违反会烧硬件）

1. **同一相上下管绝不能同时导通**：必须由 TIM1 BDTR 硬件死区（1 µs）保证，禁止软件“先开后关”；
2. `CTRL_SD` 初始化必须是低，配置完成后才拉高；任何故障先拉低；
3. 过流是**锁存**保护：复位必须 `CTRL_SD=0` → 等 200 ms → `CTRL_SD=1`；
4. 软件限流 8 A（硬件 10.4 A 锁存，电机峰值 13.5 A 不可用）；
5. 占空比钳位 [5%, 92%]，不得为“试试看”而放开；
6. 改相序只能在软件里做（电角度 +π），**不许调换电机线**（会破坏霍尔与相电压的对应）。

实际保护实现状态与已知限制仅在 [操作与验证](docs/zh-CN/09-validation-and-maintenance.md) 维护。
PWM 输出与故障关断契约见 [模块接口](docs/zh-CN/06-driver-modules.md)；不得用低频状态打印代替快速保护。

---

## 8. 构建与烧录

唯一工程为 `MDK-ARM/G474-FOC.uvprojx`。编译烧录流程和环境发现方法只在 [操作与验证](docs/zh-CN/09-validation-and-maintenance.md) 维护。

- 先发现并核实本机工具，禁止将安装路径、用户名、仓库绝对路径或本机环境清单写入受控文件。
- 工具链要求以 `.ioc`、`.uvprojx` 为准，不为适配本机改工程编译选项。
- 完成 Keil 编译、主机测试、结构检查；有 warning 必须解释，不能从编译通过推断实板通过。
- 修改时钟、引脚、DMA 必须通过 CubeMX 重新生成；SDK 和生成区域保护规则继续适用。

## 9. 文档维护

遵循第 2 节归属。文档只保存长期有效的接口、约束、操作方法与设计依据，不维护按开发轮次堆叠的工作报告。
数值配置以源码/`.ioc` 为准，文档在归属主题解释含义，其他文章链接到它，不复制参数表。

## 10. 提交前检查

- 编译、主机回归和 `bash tests/check_structure.sh` 通过；warning 已解释。
- 注释、层次依赖、ISR 时限、故障关桥及 SDK 保护规则未被绕过。
- 若契约/操作行为变更，只更新归属主题；检查中英文一致和链接有效。
- 按用户指定范围暂存，提交说明使用中文；未授权时不自动提交或推送。
- 用户明确要求仅提交代码时，不把文档混入该笔提交。

## 11. 双语约定

README.md / docs/en 为英文入口，README.zh-CN.md / docs/zh-CN 为中文入口。
每个主题保持同名中英文对照与页首导航；只同步本次受影响主题，不扩散到其他文档。
代码注释、提交说明使用中文。文档链接使用仓库相对路径；器件资料只在本地保存。
