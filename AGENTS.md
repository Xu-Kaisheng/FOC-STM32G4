# AGENTS.md — 工程规则

本文件是**权威约定**。任何人（含 AI 助手）在本仓库改动代码前必须先读本文件。
与本文件冲突的代码一律视为错误。

---

## 1. 项目上下文

| 项 | 值 |
|---|---|
| 目标 | 霍尔 FOC：开环六步 → 电流环 → 速度环，裸机实现，不引入 MCSDK |
| MCU | STM32G474RET6（LQFP64，DevEBox STM32G474R Ver.20），HSI+PLL 170 MHz；目前仅关桥诊断，板载占用已按原理图核验，J3 须空置 |
| 驱动板 | 正点原子 ATK-PD6010B（12~60 V / 10 A，6 路光耦 PWM 输入，下管 20 mΩ 采样） |
| 电机 | BL57H95：24 V，4 极（**2 对极**），3000 rpm，6.6 A，0.33 N·m，霍尔 120° |
| 控制周期 | 20 kHz（50 µs = 8500 周期 @170 MHz），中心对齐 |
| 语言 | 注释、文档、提交说明一律**中文** |

---

## 2. 引脚权威表（改动需同步 README 与 docs/zh-CN/01-hardware-and-wiring.md）

| CN3 | 信号 | G474 引脚 | 功能 |
|---:|---|---|---|
| 2 / 4 / 6 | HALL U / V / W | PC6 / PC7 / PC8 | 上拉 GPIO，当前主循环轮询 |
| 8 | VBUS | PB12 | ADC4_IN3，×25 |
| 10 / 12 / 14 | AMP_IU / IV / IW | PC0 / PC1 / PB1 | ADC1_IN6 / ADC2_IN7 / ADC3_IN1 |
| 17 / 18 | PWM_UH / UL | PA8 / PB13 | TIM1_CH1 / CH1N |
| 19 / 20 | PWM_VH / VL | PA9 / PB14 | TIM1_CH2 / CH2N |
| 21 / 22 | PWM_WH / WL | PA10 / PB15 | TIM1_CH3 / CH3N |
| 24 | CTRL_SD | PC9 | 高有效，初始化低；当前禁止拉高 |
| 7 / 16 | GND | GND | 两根均共地 |
| 15 | VTEMP | 未配置 | 暂不接 |
| 23 | 5V_IN | **禁止连接** | 避免反灌 |
| — | 调试 TX / RX | PB10 / PB11 | USART3，115200 8N1，3.3 V 逻辑 |

所选引脚已按开发板原理图与 DS12288 Rev 6 核验。**OLED/TFT J3 必须空置**，它并联 PB12～PB15、PC6/PC7。Flash 占 PA15/PC10/PC11/PC12，LED 占 PC13/PD2，按键占 PA0/PA1，均与本方案不冲突。USB PA11/PA12、SWD PA13/PA14、晶振 PF0/PF1 与 PC14/PC15 保留。

### G474RET6 配置（已核验原理图；PWM 死区已有用户实测反馈）

| 功能 | 引脚 / 配置 |
|---|---|
| TIM1 上管 / 下管 | PA8/PA9/PA10；PB13/PB14/PB15 |
| CTRL_SD | PC9，初始化低；当前板级策略禁止拉高 |
| 霍尔 U/V/W | PC6/PC7/PC8，上拉输入 |
| 电流 U/V/W | PC0 ADC1_IN6；PC1 ADC2_IN7；PB1 ADC3_IN1 |
| VBUS | PB12 ADC4_IN3，×25，每 500 ms 监视 |
| 调试串口 | PB10/PB11，USART3，115200 8N1；TX 走 DMA1_Channel2（DMAMUX USART3_TX），USART3/DMA 中断优先级 15 |
| 时钟 / PWM | HSI+PLL 170 MHz，TIM1 PSC=0、ARR=4250、RCR=1、中心对齐，20 kHz |
| 死区 / ADC | DTG=149，1 µs；ADC 内核 42.5 MHz，12 位、247.5 周期，轮询超时参数 1 ms |

2026-11-16 已核对开发板原理图及 DS12288 Rev 6，现有引脚可保留。PB12～PB15、PC6/PC7 与 OLED/TFT J3 并联，J3 必须空置。Flash 占 PA15/PC10/PC11/PC12，LED 占 PC13/PD2，按键占 PA0/PA1，均不冲突。G4 的开桥与上电 PWM 自检均禁用；2026-11-17 用户反馈临时 PWM 测试正常、死区实测 990 ns，已关闭开机 demo，保留 1 µs 硬件死区配置；VTEMP 暂不配置，USB/SWD/低速晶振接口保留，CN3-23 禁止连接。核验方法与完整排针表见 docs/zh-CN/11-g474-board-guide.md。

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

当前实现状态：按维护者要求，v0.1 的 **8 A 软件限流暂缓实现，记作待优化**；8 A 设计目标不变，不能把占空比上限或低频电流打印视为已实现限流。

VBUS 使用 ADC4 独立规则组，12 位、247.5 周期采样、轮询超时参数 1 ms；按 `raw × 3.3 × 25 / 4096` 换算。初始化或读取失败关桥并锁住启动入口，复位后重新校准；当前仅监视，不包含过欠压保护或 VDDA 校准。

G474 当前三个电流 ADC 是低频依次软件触发，不能标记为三 ADC 同步采集或已实现闭环/快速保护。

六步输出由 `bsp_pwm_apply_pair()` 使用软件 COM 同时提交模式和使能：高相 PWM1、低相强制无效、第三相两路关闭。三相 CCR 维护相同非零占空比，写入通过短暂 UDIS 成组保护，运行换相不触发 UG；输出接口仅主循环单一写入方调用。启动和开桥前提交模式及 CCR，停机／异常先关 SD/MOE，再清使能并提交 COM。当前开桥禁用策略不变；同步 ADC 或电流环接入前需重新设计更新窗口。

---

## 8. 构建与烧录

唯一 Keil 工程：`MDK-ARM/G474-FOC.uvprojx`，可通过根目录 `STM32-FOC.uvmpw` 打开。

- Agent 编译前先从 `PATH` 查找 `UV4.exe`；未找到时查询 Windows Keil 安装注册信息（包括 32 位注册表视图）或已有安装位置，并验证可执行文件存在。
- 仅在当前终端设置 `KEIL_UV4`；禁止将本机安装路径、用户名、仓库绝对路径和本机环境清单写入受版本控制的文件。查找示例见 `docs/zh-CN/09-validation-and-maintenance.md`。
- 工具链与设备包要求从 `.ioc`、`.uvprojx` 读取并与本机已安装环境核对，不为适配本机擅改工程编译选项。
- Windows 在仓库根目录的 Git Bash 执行，工程、日志和测试参数均使用仓库相对路径：

```bash
: "${KEIL_UV4:=$(command -v UV4.exe || true)}"
: "${KEIL_UV4:?Locate UV4.exe and set KEIL_UV4 for this shell first}"
"$KEIL_UV4" -r "MDK-ARM/G474-FOC.uvprojx" -j0 -o "../build-g474.log"
python -X utf8 tests/run_native.py
bash tests/check_structure.sh
```

- 编译产物：`MDK-ARM/G474-FOC/G474-FOC.axf|.hex`。
- 编译后必须检查 `build-g474.log` 的 `0 Error(s)`，有 warning 要说明原因。
- 修改时钟、引脚、DMA 后必须回到 CubeMX 重新生成，不要手改生成代码。
- 下载配置尚未实板验证；连接调试器后按 STM32G474RET6 选择目标，不能由编译通过推断烧录已验证。

---

## 9. 文档要求

- 每完成一个模块（`drv/`、`foc/` 下的一个 `.c/.h`），在 `docs/` 下补一篇说明：
  功能、接口、调用时序、已知限制；
- 修改引脚、限幅、控制参数时，同步更新 `README.md` 的“关键设计参数”与 `docs/zh-CN/01-hardware-and-wiring.md`；
- 每升一个版本，记录：改了什么、为什么、实测数据（ISR 周期、抖动、波形）。

---

## 10. 提交前检查清单

- [ ] 编译 0 error，warning 已解释
- [ ] 涉及引脚/参数改动 → README、docs/zh-CN/01-hardware-and-wiring.md、AGENTS.md 三处同步
- [ ] ISR 内没有阻塞调用、没有 printf
- [ ] 新增魔法数字已具名化
- [ ] 函数有头部注释，关键逻辑写了 WHY
- [ ] 安全红线（死区、CTRL_SD 时序、限流）未被绕过
- [ ] 涉及硬件行为的改动，已在文档中写清验证方法

## 11. 双语文档约定

- README.md 与 docs/en/ 为默认英文入口；README.zh-CN.md 与 docs/zh-CN/ 为对应中文版。
- 同一主题两种语言使用相同文档编号与文件名，页首提供语言切换和目录链接。
- 修改行为、引脚、参数或验证结论时同步维护两种语言；代码注释与提交说明仍使用中文。
- docs/README.md 为双语总目录；docs 内源码路径默认相对于仓库根目录，实际链接须使用正确相对路径。
