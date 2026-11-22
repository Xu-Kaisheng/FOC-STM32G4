# 07 代码架构与应用编排

[English](../en/07-architecture-and-flow.md) | **简体中文** | [文档目录](../README.md)

本页维护职责和调用关系；强制编码规则在 [AGENTS](../../AGENTS.md)，操作与启动许可在 [09](09-validation-and-maintenance.md)，寄存器/模块契约在 [06](06-driver-modules.md)。

## 目录与依赖

```text
src/app/             应用编排、命令、控制策略、日志、性能、状态
src/algorithm/       纯算法，通过注入回调输出，不依赖应用或 HAL
src/bsp/             板级参数和硬件原语
src/third_party/     第三方实现
Core/                CubeMX 初始化；USER CODE 连接应用与异常钩子
Drivers/             只读 HAL/CMSIS
```

允许 `app → algorithm / bsp / third_party`；下层不得反向依赖应用。BSP 只依赖自身公开接口与 board，纯内联时基例外；PWM 内部由 `pwm.c` 编排同模块原语。BSP 不含任务。

| 应用模块 | 职责 |
|---|---|
| app/app | 初始化、模式切换、主循环；app_state 封装共享状态 |
| app/cmd | 非阻塞接收单字符并分发命令 |
| app/control | 保存策略参数、步号和调度时间；注入 BSP 输出回调 |
| app/status | 慢速采样与状态报告 |
| app/log | 静态日志队列与 DMA 推进 |
| app/perf | 工作/中断/控制耗时统计，主循环格式化输出 |

跨模块通过公开函数通信；ADC 句柄留在 BSP，算法不访问 HAL。Keil 源文件分为 App、Algo、BSP 三个工程组，包含路径与目录一致。

## 启动与主循环

`Core main → app_init → app_task`。初始化先准备串口和参数，再关桥初始化 PWM，校准电流与母线 ADC、试读母线、初始化霍尔；可选波形自检全程关桥。启动日志之后初始化性能统计，避免把校准计入运行窗口。

主循环依次推进日志发送、命令、控制、霍尔监视、状态及性能报告；霍尔监视按模式运行。没有控制 ISR，未来中断控制接入需要单独设计时序。

## 模式切换

相同模式请求不重复启动。真正切换时先关桥停 PWM，再完成驱动锁存复位等待。启动请求依次检查板级许可、ADC 就绪和 PWM 输出联锁；任何失败退回 STOP。具体命令、等待时间和参数由 [09](09-validation-and-maintenance.md) 维护。

六步由 `app_control_task → alg_sixstep_next_step / alg_sixstep_apply → 注入的 BSP 回调` 推进。算法选择相组合，BSP 负责提交；COM/UEV 语义仅在 [06](06-driver-modules.md) 维护。

## 错误与恢复边界

- ADC 采集失败或样本过期：关桥并锁住启动，复位后重新校准；不得把旧样本当新值。
- 输出提交/开桥检查失败：立即停 PWM、关桥并回到 STOP。
- 致命异常：Core USER CODE 调用 BSP 紧急关断，不能依赖串口或延时。
- 日志错误与性能计时失效由各模块处理，不自动启动功率级；接口见 [06](06-driver-modules.md) 与 [12](12-performance-monitoring.md)。

`tests/check_structure.sh` 检查依赖与命名；软件回归和硬件验证入口见 [09](09-validation-and-maintenance.md)。
