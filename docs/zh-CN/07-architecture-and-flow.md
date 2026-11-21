# 07 代码架构与流程

[English](../en/07-architecture-and-flow.md) | **简体中文** | [文档目录](../README.md)

本文描述当前关桥诊断固件及保留的 v0.1 六步路径。接口与限制详见 [06-driver-modules.md](06-driver-modules.md)，历史问题证据详见 [08-review-findings.md](08-review-findings.md)。

## 1. 模块边界

- Core：CubeMX 生成时钟、GPIO、ADC、TIM1、USART3；USER CODE 区调用 app_init/app_task 并接入故障关断。
- `app/app`：`app`（初始化、模式联锁与主循环）、`app_state`（模式、时间戳、霍尔、ADC 可用性）。
- `app/cmd`：`app_cmd_task()`：非阻塞读命令并解析。
- `app/control`：算法策略：六步参数（步号、占空比、电频率）、台架限幅与 BSP 回调注入。
- `app/log`：`app_log_task()`：日志格式化、512 字节队列与 DMA 推进。
- `app/status`：`app_status_task()`：周期性状态打印。
- `app/perf`：DWT 控制任务计时与有效 CPU 工作负载统计，每秒输出，详见 [12-performance-monitoring.md](12-performance-monitoring.md)。
- `algorithm/sixstep`：换相表与步进规则，纯算法；硬件输出经注入回调完成。
- `algorithm/foc`、`third_party`：预留。
- `bsp/pwm`：六路输出、占空比、悬空相、三相/六步联锁、CTRL_SD 和紧急关断。
- `bsp/adc_cur`：ADC 校准、三个规则组依次读取、完整样本发布及失败返回。
- `bsp/adc_vbus`：ADC4 校准、规则组单次采集及母线电压换算，详见 [10-bus-voltage-adc.md](10-bus-voltage-adc.md)。
- `bsp/hall`：读取原始编码、相对扇区查表、状态字符串。
- `bsp/uart`：USART3 硬件接口：启动一次 TX DMA、查询忙、非阻塞收字节；**无队列、无 task**。
- `bsp/board`：板级常量与时基，是 BSP 内唯一允许被其他模块依赖的模块。
- `bsp/time`：`cycle.h`，DWT 周期计数，全部 `static inline`。

依赖方向严格单向：应用层可调用 `algorithm`、`third_party`、`bsp`；`algorithm`/`third_party` 不得依赖 app/bsp；`bsp` 只依赖 `bsp/board`，且不允许出现 `*_task()`。`bash tests/check_structure.sh` 会检查这些禁令、app 子目录的 task 入口与分节模板。

应用通过公开接口使用驱动。ADC 句柄由各平台 ADC 实现内部管理，应用调用无参数初始化；串口句柄只由串口模块访问。`bsp/board` 提供时基、PWM 参数、平台名称和驱动许可。没有控制 ISR，当前采样仍是主循环轮询。G474 默认关闭 PWM 自检，应用和 PWM 驱动两处均拒绝开桥，详见 docs/zh-CN/11-g474-board-guide.md。

## 2. 启动流程

```text
HAL_Init → 时钟 → GPIO（SD 默认低）→ ADC/TIM1/USART3
  → app_init
      → 串口队列初始化
      → bsp_pwm_init：关桥、停 PWM、占空比清零
      → ADC1/2/3 校准；失败锁住启动入口
      → ADC4 校准并试读 VBUS；失败锁住启动入口
      → 霍尔初始化
      → G474 上电 PWM 自检禁用，保持关桥和 PWM 停止
      → 停 PWM、占空比清零、输出启动状态
  → while(1) app_task
```

自检只允许在 SD=0 下运行。配置失败不代表可以跳过检查强行开桥。应用初始模式为 STOP。

## 3. 主循环

```text
app_task
  → app_log_task()        日志后台发送（DMA）
  → app_cmd_task()        非阻塞读一个命令并处理
  → app_control_task()    六步模式按步间隔推进（算法 + BSP 回调）
  → 霍尔监视模式仅在状态变化时输出
  → app_status_task()     每 500 ms 读 ADC4/ADC1-3 并输出状态
  → app_perf_task()       每秒输出性能报告
```

步间隔为 `floor(1000/(6×f_e))` ms；每次调度至多推进一步，没有实时控制环频率保证。日志发送不再等待串口，但格式化、ADC 轮询及主循环本身仍有开销。

## 4. 模式切换与启动

实际模式切换先执行 SD=0 → bsp_pwm_stop → 等 200 ms。停止动作发生在等待之前。相同模式命令直接返回，不重复启停。

当前 G474 策略直接拒绝正/反转命令。以下为保留的六步控制路径，尚未在本板解锁验证：

1. ADC 未就绪则保持 STOP，并提示复位。
2. 三相预装入相同占空比、启动 TIM1，提交 PWM1 模式和比较值后进行三相输出自检。
3. `alg_sixstep_apply` 维护三相相同占空比，通过应用注入的回调用 COM 一次提交高相 PWM1、低相强制无效和第三相关闭。
4. 六步专用入口按当前 high/low 精确验证两相输出、模式及相同非零 CCR，提交模式和占空比预装载后拉高 SD。
5. 联锁拒绝时关桥、停 PWM、回到 STOP；只有成功才输出 SIXSTEP 状态。

三相自检和六步开桥检查不能混用：前者要求 0x555，后者要求当前两相的精确掩码。使能输出通道不等于 MOS 管同时导通，互补关系和死区仍由 TIM1 硬件产生。

运行中的换相仅通过 COM 改变输出角色，不复位计数器；占空比通过 UEV 成组更新。输出提交失败时立即关桥停 PWM 并回到 STOP。接口限于主循环单一写入方，调用约束和验证方法见 [PWM 驱动](06-driver-modules.md)。

## 5. 错误路径

- ADC 初始化失败：禁止启动，仍允许停机和霍尔监视。
- ADC 采样失败：先关桥停机，锁住启动入口直至复位，报告错误；不会把零初始化样本打印为正常 0 A。
- PWM 开桥联锁失败：驱动确保 SD=0，应用进入 STOP。
- 致命异常或 Error_Handler：先调用 bsp_pwm_stop_emergency，再进入死循环；清除 MOE/AOE、六路输出与 CEN，无日志、无延时。

8 A 软件过流判断及故障锁存按维护者要求暂缓，ADC 通信/转换失败保护不等同于软件限流。

## 6. 必须保持的约束

- 开桥必须通过对应模式的输出检查；停止或致命异常先关功率级。
- 初始化与重新开桥都不能依赖残留 CCR，启动及开桥前显式提交预装载。
- 单相悬空不停止 TIM1；不使用软件顺序代替硬件死区。
- 200 ms 等待只在主循环且 PWM 已停止时发生，不能放到异常处理或未来控制 ISR。
- ADC 样本只有返回成功才有效；霍尔相对角度须完成方向及零点标定后才能用于闭环。
- ISR 内禁止阻塞、日志及延时；未来 20 kHz 电流环需重新核对 ADC 触发、更新事件和控制时序。
