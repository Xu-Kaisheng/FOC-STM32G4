# FOC-STM32G4

[English](README.md) | **简体中文**

基于 **STM32G474RET6（DevEBox Ver.20）+ ATK-PD6010B + BL57H95** 的裸机霍尔 FOC 工程，不引入 MCSDK。当前唯一目标为 G474，处于关桥诊断阶段，尚未实现电流环、速度环及 8 A 快速软件限流。

打开根目录 [STM32-FOC.uvmpw](STM32-FOC.uvmpw)，或直接打开 [G474-FOC.uvprojx](MDK-ARM/G474-FOC.uvprojx)。工作区只包含一个目标。

完整文档：[文档目录](docs/README.md)。器件资料 `datasheet/` 仅本地保存，不随仓库发布；原厂 SDK 保留各自许可证。

## 目录结构

```text
STM32-FOC/
├── STM32-FOC.uvmpw           单目标 Keil 入口
├── G474-FOC.ioc              唯一 CubeMX 配置源
├── Core/                    生成的启动初始化与中断接口
├── Drivers/                 原厂 HAL/CMSIS，只读
├── MDK-ARM/                 Keil 工程、启动文件及编译产物
├── platform/                板级参数、ADC1/2/3 电流、ADC4 母线实现
├── src/
│   ├── app/                 命令、状态管理、六步调度及板级接口
│   ├── drv/                 PWM、霍尔实现及 ADC 接口
│   └── bsp/                 串口队列、DWT 性能统计
├── tests/                   主机回归与 HAL 替身
├── docs/en/                 英文文档
├── docs/zh-CN/              中文文档
└── datasheet/               原始器件资料，只读，不参与构建
```

生成代码只改 USER CODE 区；外设配置改根目录下的 `.ioc` 并通过 CubeMX 重新生成。平台 ADC 的实现只保留在 `platform/`，`src/drv/` 提供接口，不再有同名的其他平台实现。

`tests/` 是回归测试源码，保留用于检查保护逻辑、ADC 和性能统计。运行测试时会自动创建 `build/native-tests/` 存放产物，该目录可随时清理。下载缓存、分析临时文件和驱动安装包不需要放在工程内。

## 当前功能

- USART3 命令与非阻塞日志；0 停机、h 霍尔监视、? 帮助。
- 霍尔原始状态读取及相对扇区查表，尚未完成转子角度标定。
- 每 500 ms 依次读取 ADC1/2/3 三相电流，ADC4 独立读取母线电压；失败关桥并锁住启动入口。
- TIM1 六路互补 PWM 与 1 µs 死区已配置，**上电 PWM 自检和开桥均禁用**，1/2 转动命令被拒绝。2026-11-17 用户反馈开机 demo 测试正常、死区实测 990 ns；临时 demo 已关闭，硬件死区配置保留。
- 每秒输出有效 CPU 工作占比、工作/空轮询/中断耗时；控制任务统计支持平均/最大值和电流环超时计数。当前 `CURRENT=OFF`，详见 [性能统计](docs/zh-CN/12-performance-monitoring.md)。

## 关键设计参数

| 项目 | 当前配置 / 目标 |
|---|---|
| MCU | STM32G474RET6，LQFP64，Cortex-M4F，512 KB Flash、128 KB SRAM |
| 时钟 | HSI 16 MHz / 4 × 85 / 2 = 170 MHz |
| PWM | 中心对齐 20 kHz，PSC=0、ARR=4250、RCR=1 |
| 死区 | DTG=149，CKD=1，1 µs |
| 电流采样 | ADC1/2/3 独立规则组，依次软件触发；尚未同步采样 |
| VBUS | ADC4_IN3，PB12，`raw × 3.3 × 25 / 4096`；当前仅监视 |
| ADC 参数 | 42.5 MHz 内核、12 位、247.5 周期，轮询超时参数 1 ms |
| 占空比 | PWM 接口 5%～92%；六步调试 5%～60%，当前不能启动 |
| 控制环目标 | 电流环 20 kHz、速度环 1 kHz，均未实现；每 50 µs 有 8500 CPU 周期 |
| 保护 | 8 A 软件限流待实现；驱动板硬件 10.4 A 锁存 |
| 电机 | 24 V、4 极（2 对极）、3000 rpm、6.6 A、0.33 N·m |

## 接线

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

完整 J4/J5 脚号、背面位置、供电与 MCU 端逻辑分析仪接线见 [G474 工程与接线](docs/zh-CN/11-g474-board-guide.md)。驱动板与电机接线见 [硬件架构](docs/zh-CN/01-hardware-and-wiring.md)。

## 编译与验证

软件环境：CubeMX 6.16.1、STM32CubeG4 v1.6.3、STM32G4xx_DFP 1.5.0、ARM Compiler 5.06 update 7。Windows 下在 Git Bash 执行：

```bash
"C:/Keil_v5/UV4/UV4.exe" -r "E:/Code/STM32-FOC/MDK-ARM/G474-FOC.uvprojx" -j0 -o "E:/Code/STM32-FOC/build-g474.log"
python -X utf8 tests/run_native.py
```

固件输出：`MDK-ARM/G474-FOC/G474-FOC.axf|.hex`。检查日志中的 `0 Error(s)` 并说明 warning。用户已完成临时 PWM 测试并反馈死区为 990 ns；demo 已关闭。下载工具与接线需按实际探针确认。测试与实板验收范围见 [验证与维护](docs/zh-CN/09-validation-and-maintenance.md)。
