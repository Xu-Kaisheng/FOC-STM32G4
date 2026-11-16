# 04 CubeMX 配置说明

[English](../en/04-cubemx-configuration.md) | **简体中文** | [文档目录](../README.md)

唯一配置源为 `G474-FOC.ioc`。工具为 CubeMX 6.16.1、STM32CubeG4 v1.6.3、STM32G4xx_DFP 1.5.0；Keil 使用 ARM Compiler 5.06 update 7。

## 当前生成配置

| 模块 | 配置 |
|---|---|
| MCU | STM32G474RET6，LQFP64 |
| RCC | HSI 16 MHz，PLLM=4、PLLN=85、PLLR=2，SYSCLK=170 MHz |
| TIM1 | PSC=0、ARR=4250、RCR=1，中心对齐模式 1，PWM1，主/互补输出高有效 |
| BDTR | CKD=1、DTG=149，死区 1 µs，OSSI/OSSR 开启，AOE 关闭 |
| GPIO | PC9 输出初始化低；PC6/PC7/PC8 上拉输入 |
| ADC1 | PC0 / IN6，相电流 U |
| ADC2 | PC1 / IN7，相电流 V |
| ADC3 | PB1 / IN1，相电流 W |
| ADC4 | PB12 / IN3，母线分压 |
| ADC 公共参数 | 独立模式、SYSCLK/4=42.5 MHz、12 位右对齐、247.5 周期、规则组单通道软件触发 |
| USART3 | PB10 TX / PB11 RX，115200 8N1 |
| 调试 | PA13 SWDIO / PA14 SWCLK |

PA8/PA9/PA10 为 TIM1_CH1/2/3（AF6），PB13/PB14 为 CH1N/2N（AF6），PB15 为 CH3N（AF4）。详细接线及板载占用见 docs/zh-CN/01-hardware-and-wiring.md、docs/zh-CN/11-g474-board-guide.md。

`(64+21)×2/170 MHz = 1 µs` 对应 DTG=149。当前 ADC 单通道理论转换时间约 `(247.5+12.5)/42.5 MHz = 6.12 µs`，三路依次软件调用并非同步采样。TIM1 的 20 kHz 配置不代表电流环已经运行。

## 重新生成注意事项

1. 只修改 `.ioc` 并经 CubeMX 重新生成外设配置，不手改生成区。
2. 保留 `Core` 的 USER CODE 中应用入口、异常关桥及 SysTick 性能统计钩子。
3. 确认 Keil 仍引用 `src/app`、`src/drv`、`src/bsp` 与本板 `platform` 的源码条目。
4. HAL/CMSIS、启动文件及编译选项按工程规则保护；SDK 不手工修改。
5. 重编译并检查 0 error / warning；硬件时序修改后需重新测量，不能用配置值代替实测。

当前板级策略禁止开桥及上电 PWM 自检，临时抓波 demo 已完成测试并关闭，没有新增同步 ADC 触发、中断电流环或快速保护。
