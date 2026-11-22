# 04 CubeMX 配置维护

[English](../en/04-cubemx-configuration.md) | **简体中文** | [文档目录](../README.md)

本页维护生成配置的来源和再生成流程，不复制接线表或运行策略。

## 配置来源

| 内容 | 唯一来源 |
|---|---|
| 时钟、GPIO、定时器、ADC、DMA、串口 | [G474-FOC.ioc](../../G474-FOC.ioc) |
| 工具链、设备包、源文件与包含路径 | [Keil 工程](../../MDK-ARM/G474-FOC.uvprojx) |
| 运行时板级校验参数 | [board.c](../../src/bsp/board/board.c) |
| TIM1 模式提交与使能 | [PWM 模块契约](06-driver-modules.md) |
| ADC4 序列与参考电压采集 | [母线/VREF](10-bus-voltage-adc.md) |

生成值和运行时覆盖必须分别核对。例如 PWM 启停会管理 AOE、CCPC、COM，不可仅从 CubeMX 初始模式推断运行输出。引脚分配见 [01](01-hardware-and-wiring.md)，复用/板载占用证据见 [11](11-g474-board-guide.md)。

## 重新生成

1. 修改 `.ioc`，通过 CubeMX 生成，禁止手改 Core 非 USER CODE 区和 HAL/CMSIS。
2. 保留应用入口、异常关桥、IRQ 性能钩子，以及 Keil 的 App/Algo/BSP 源文件和包含路径。
3. 串口 DMA 和 NVIC 配置由 CubeMX 生成，BSP 不自行补开时钟；不要改编译选项适配本机环境。
4. 对比生成差异，确认没有丢失用户代码，再按 [09](09-validation-and-maintenance.md) 编译、回归和检查结构。
5. 时钟、引脚、采样触发或输出时序改变后，执行对应模块硬件验证；配置值不是实测结果。
