# 06 驱动模块说明（v0.1）

[English](../en/06-driver-modules.md) | **简体中文** | [文档目录](../README.md)

本文对应当前 `src/` 模块。CubeMX 外设初始化完成后由 `app/app/app.c` 的 `app_init` 组装调用；Core 只在 USER CODE 区接入应用和故障关断。

当前仅支持 G474：应用通过 `bsp/board` 获取板级参数；电流和母线 ADC 实现与接口同在 `bsp/adc_cur/`、`bsp/adc_vbus/`。目录分层与依赖规则见 [13-repo-restructure.md](13-repo-restructure.md)。

## 1. PWM 驱动：bsp/pwm

| 接口 | 功能与约束 |
|---|---|
| `bsp_pwm_init()` | 关桥、停止输出、占空比清零 |
| `bsp_pwm_start()` | 关桥状态下复位计数并提交预装载，再使能六路输出、MOE 和计数器 |
| `bsp_pwm_check_ready()` | 三相自检，必须六路成对使能 |
| `bsp_pwm_set_duty(du,dv,dw)` | FOC 用，钳位 5%～92% |
| `bsp_pwm_set_duty_raw(du,dv,dw)` | 允许初始化清零，上限 92%；六步运行时三相维护相同非零占空比 |
| `bsp_pwm_apply_pair(high,low)` | 准备三相模式与使能，再用单次 COM 提交；返回 1 成功、0 拒绝，失败后调用方必须停机 |
| `bsp_pwm_enable_bridge(en)` | 三相开桥或无条件关桥；返回 1 成功、0 拒绝 |
| `bsp_pwm_enable_bridge_sixstep(high,low)` | 指定两相成对使能、第三相悬空才开桥；返回 1 成功、0 拒绝且已关桥 |
| `bsp_pwm_is_bridge_enabled()` | 读取桥使能的软件影子，不代表硬件过流锁存已解除 |
| `bsp_pwm_stop()` | 先关桥，再清 MOE/AOE、六路使能和计数器 |
| `bsp_pwm_stop_emergency()` | 致命异常关断，先写 GPIO BRR 拉低 SD，再清 TIM1 输出及计数器，不依赖 HAL 句柄或延时 |

### 启动与联锁

当前 G474 的开桥策略禁止使能。保留的六步驱动路径供后续验证，启动流程为：关桥 → 停 PWM → 等 200 ms → 三相装入相同占空比 → bsp_pwm_start → 三相自检 → alg_sixstep_apply → 六步专用开桥检查。启动在 MOE 关闭时启用 CCPC、关闭外部 COM 触发并恢复三相 PWM1，提交模式和占空比后才置 MOE/CEN。任一步拒绝开桥，应用退回 STOP。

两类联锁共用波形配置检查：MOE/CEN 有效、ARR=4250、PSC=0、RCR=1、中心对齐模式 1、CKD=1、DTG=149、OSSI/OSSR 有效、CCR 预装载及主/互补输出高有效；要求 CCPC=1、CCUS=0、UDIS=0、三相空闲电平为低。三相自检要求 PWM1 和使能掩码 0x555；六步入口要求指定两相掩码、高相 PWM1、其余相强制无效，以及三路 CCR 相同且非零。

开桥前提交 COM 和更新事件，确保读回的模式、使能和 CCR 预装载均已生效。运行中换相只产生 COM，不使用 UG、不重置计数器。该动作适用于当前软件触发 ADC 的 v0.1；后续改硬件触发时必须重新核对更新事件和采样时序。

使能 CCxE/CCxNE 是允许输出互补 PWM，不代表上下管同时导通。HAL_TIM_PWM_Start 只开启主输出，互补输出由 HAL_TIMEx_PWMN_Start 单独控制；本模块显式管理六路使能。

### COM 输出组合与调用约束

| 角色 | OCxM | CCxE / CCxNE | 输出 |
|---|---|---|---|
| 高相 | PWM1 | 1 / 1 | 高侧斩波、低侧互补，保留硬件死区 |
| 低相 | 强制无效 | 1 / 1 | OCREF 无效，高侧关、低侧常通 |
| 悬空相 | 强制无效 | 0 / 0 | 两路关闭 |

算法通过应用注入的 `apply_pair` 回调一次提交组合，返回失败时应用关桥停 PWM 并退回 STOP。三相 CCR 先维护可用占空比；调占空比在下一次允许的 UEV 生效，COM 只提交模式与使能。因此占空比尚未更新时，换相仍使用上一份完整占空比。COM 不自动对齐载波边界，引脚边沿还受 CNT 和死区影响。

输出接口只由主循环单一调用方使用，不能与 ISR 中的输出配置并发。CCR 换算先完成，再用 UDIS 保护三次寄存器写入并恢复原状态；期间计数器继续运行，但恰好遇到的更新事件会被禁止，更新/TRGO 可能延后。改成同步 ADC 或电流环前必须重新设计更新窗口。

实板验证先保持 CTRL_SD=0，观察全部六路 PWM：覆盖六步正反向、低相常通、第三相关闭、调占空比、停止再启动及故障关断；核对 COM 切换附近的互补死区和载波连续性。主机模型仅验证 UEV/COM 寄存器提交语义，不代表电气波形或功率级验证。

### 故障关断

NMI、HardFault、MemManage、BusFault、UsageFault 及 Error_Handler 在死循环前调用 bsp_pwm_stop_emergency。先拉低 SD、清除 MOE/AOE，再清全部使能并用 COM 提交，最后停计数器；关断不等待自然更新。清除 AOE 是为了防止更新事件自动恢复 MOE。正常停机同样清除待提交使能。故障后保持停机直至系统复位；该函数不能处理 CPU 已完全失去执行能力的情况。

## 2. 电流采样：bsp/adc_cur

| 接口 | 返回值与约束 |
|---|---|
| `uint8_t bsp_adc_cur_init(void)` | 关桥时调用，句柄由平台管理；1 成功，0 失败，失败后保持关桥并复位重试 |
| `uint8_t bsp_adc_cur_read(adc_cur_sample_t *out)` | 1 成功；0 失败且 out 保持原值，不得使用旧样本 |

初始化：依次校准 ADC1/ADC2/ADC3，任一路失败保持未就绪。通道由 CubeMX 生成，分别为 PC0/IN6、PC1/IN7、PB1/IN1。

读取：依次软件启动每个 ADC 的规则组 → 轮询完成（超时参数 1 ms）→ 读取单通道结果 → 停止转换。仅三路全部成功才发布完整样本；任何失败不覆盖调用者的输出，应用关桥并锁住启动入口。

换算保持 `I=(raw×3.3/4095−1.25)/0.12`。HAL 自校准只校准 ADC 自身，不等于测量驱动板放大器零漂；真实 VDDA 和每相零偏仍待标定。

v0.1 每 500 ms 软件采样仅用于状态监视，未与下管采样窗口同步，不能当作可靠的瞬时相电流或快速过流保护。8 A 软件限流按维护者要求暂缓，后续需先完善同步采样。

## 3. 霍尔：bsp/hall

PC6/PC7/PC8 上拉输入，原始编码 U/V/W 分别位于 bit2/bit1/bit0。bsp_hall_read 返回原始编码 1～6，000/111 返回 HALL_INVALID；bsp_hall_get_state_str 用于打印原始状态。

调用顺序：bsp_hall_read → 排除 HALL_INVALID → bsp_hall_get_sector_deg。

| 原始编码 | 1 | 3 | 2 | 6 | 4 | 5 |
|---|---|---|---|---|---|---|
| 相对扇区角度 | 0° | 60° | 120° | 180° | 240° | 300° |

该表来自样例位序换算，证据见 [08-review-findings.md](08-review-findings.md)。S1 只是相对零点，实际方向和转子电角度偏移仍待标定；该角度函数当前未被开环控制调用。非法输入仍返回 0，不能仅凭角度值判断状态有效性。

## 4. 调试串口：bsp/uart + app/log

硬件与业务分离：

| 模块 | 接口 | 职责 |
|---|---|---|
| `bsp/uart` | `bsp_uart_init()` | 清 ORE、复位发送忙标志 |
| `bsp/uart` | `bsp_uart_write(data, len)` | 启动一次 USART3_TX DMA；不拷贝、不等待 |
| `bsp/uart` | `bsp_uart_is_tx_busy()` | 查询 DMA 是否仍在发送 |
| `bsp/uart` | `bsp_uart_read_byte(out)` | 非阻塞收一字节并清接收溢出标志 |
| `bsp/uart` | `HAL_UART_TxCpltCallback()` | TC 中断里只清忙标志 |
| `bsp/uart` | `HAL_UART_ErrorCallback()` | TX DMA 失败时清忙标志；接收错误不释放发送缓冲区 |
| `app/log` | `app_log_init()` | 复位 512 字节静态队列 |
| `app/log` | `app_log_write(fmt,...)` | 格式化并入队；单条最多 127 字节，空间不足整条丢弃 |
| `app/log` | `app_log_task()` | 主循环推进：上一段发完后，把队首连续区间交给 `bsp_uart_write()` |

`bsp/uart` 不含队列、不格式化、**没有 `_task()`**；"每轮主循环推进"属于应用层，见 `app/log`。

**发送路径（DMA）**：CubeMX 配置 DMA1_Channel2 + DMAMUX 请求 `USART3_TX`（NORMAL、字节对齐、内存递增），并打开 DMA1_Channel2 与 USART3 两个中断（优先级 15，最低）。`app_log_task()` 在 `bsp_uart_is_tx_busy()` 为 0 时推进：先按上一段长度前移队尾，再取队首连续区间交给 `bsp_uart_write()`；跨缓冲区末尾时先发到边界，剩余部分下一轮再发。DMA 完成由 BSP 的 TC 回调清忙标志，队列推进由应用层轮询完成——BSP 不需要反向调用 app。

**发送错误**：USART3 当前仅 TX 使用 DMA。HAL 终止失败的 DMA 发送后，错误回调仅清忙标志；主循环按原段长释放队列并发送下一段，不重发失败段，因此可能丢失该段剩余字节。接收溢出及其他串口的错误不清 TX 忙标志，避免释放仍在发送的缓冲区。主机回归验证发送错误后下一段继续发送、接收错误不干扰 TX；实板保持关桥，检查错误注入后的日志恢复与接收独立性。

计时由 `app_log_task()` 在确认有队列工作后包裹回收与 DMA 提交；空队列和 DMA 忙时返回均归入 `poll`。提交失败的尝试仍属于工作，重试留给后续主循环。BSP 不依赖应用层性能统计。

## 5. 应用与已知限制

应用层按职责拆分：`app/app`（初始化、模式联锁、主循环、共享状态）、`app/cmd`（命令）、`app/control`（算法策略）、`app/log`（日志队列）、`app/perf`（性能统计）、`app/status`（状态打印）；每个子文件夹都提供 `*_task()`，由 `app_task()` 统一调度。

串口命令：0 停机，1/2 为正反转请求（当前拒绝），h 霍尔监视，+/- 调整占空比，>/< 调整电频率，? 打印帮助。帮助分条入队，避免超过格式化缓冲区。

六步表保持 U+V-、U+W-、V+W-、V+U-、W+U-、W+V-；只关当前悬空相，高相斩波、低相上管 0%。占空比范围 5%～60%，电频率 0.5～20 Hz。换相仍为毫秒级主循环调度，存在时间量化和任务抖动；尚无闭环、同步采样或软件限流。

## 6. 六步策略与算法：app/control + algorithm/sixstep

| 模块 | 职责 |
|---|---|
| `algorithm/sixstep` | 换相表、下一步计算、按步施加三相。**纯算法**，只 include 标准 C；硬件输出通过 `alg_sixstep_io_t` 回调完成 |
| `app/control` | 六步参数（步号、占空比、电频率）与台架限幅；把 BSP 的 `bsp_pwm_apply_pair/set_duty_raw` 注入算法；`app_control_task()` 按步间隔推进 |

调用关系：

```text
app_task → app_control_task → alg_sixstep_next_step / alg_sixstep_apply
                                      │ 回调
                                      ▼
                              bsp_pwm_apply_pair / bsp_pwm_set_duty_raw
```

算法层不 include 任何 BSP/应用头文件，因此可以脱离硬件做主机测试，也能被将来的 FOC 复用同一套回调机制。开桥联锁仍由 `app/control` 调用 `bsp_pwm_enable_bridge_sixstep()` 完成。

硬件验证方法及本次软件验证结果见 [09-validation-and-maintenance.md](09-validation-and-maintenance.md)。
