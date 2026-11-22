# 11 G474 工程与接线核验

[English](../en/11-g474-board-guide.md) | **简体中文** | [文档目录](../README.md)

本页只维护 DevEBox Ver.20 的物理排针、板载网络与供电参考；CN2/CN3 接线以 [01](01-hardware-and-wiring.md) 为准，操作与构建见 [09](09-validation-and-maintenance.md)。

## 照片能证明什么

用户确认芯片为 STM32G474RET6；照片可见 DevEBox 标识、左右 15×2 排针、SWD、原生 Type-C USB、W25Q16 Flash、OLED/TFT 接口、LED 和按键，以及标注的 8 MHz / 32.768 kHz 晶振。新增实物背面照可读到 `DevEBox / STM32G474R / Ver.20`，据此记录板卡标识。2026-11-16 已补充开发板原理图与 DS12288 Rev 6 数据手册核验。

照片证明排针丝印与引出位置，**不能证明这些 GPIO 未连接板载器件**。下表以新增的**实物背面照**为准：能看到 `STM32G474R / Ver.20` 丝印，USB 在上；左右均从顶端电源脚开始数排。“外侧”靠板边，“内侧”靠板中心。它与先前正面产品图左右镜像；翻到元件面时不能照搬这里的左右位置。

| 排 | 左排外侧 | 左排内侧 | 右排内侧 | 右排外侧 |
|---:|---|---|---|---|
| 1 | 3V3 | 3V3 | 5V | 5V |
| 2 | VREF+ | VDDA | GND | GND |
| 3 | GND | GND | PB12 | RST |
| 4 | PB11 | PB10 | PB13 | PB14 |
| 5 | PB1 | PB2 | PB15 | PC6 |
| 6 | PC5 | PB0 | PC7 | PC8 |
| 7 | PA7 | PC4 | PC9 | PA8 |
| 8 | PA5 | PA6 | PA9 | PA10 |
| 9 | PA3 | PA4 | PA11 | PA12 |
| 10 | PA1 | PA2 | PA15 | PC10 |
| 11 | PC3 | PA0 | PC11 | PC12 |
| 12 | PC1 | PC2 | PD2 | PB3 |
| 13 | PC15 | PC0 | PB4 | PB5 |
| 14 | PC13 | PC14 | PB6 | PB7 |
| 15 | E_BAT | GND | PB8 | PB9 |

VDDA、VREF+、E_BAT 是电源/参考/备份域端子，不作为通用 GPIO。原理图确认 3V3 经 R11（0 Ω）接 VDDA，VDDA 经 R10（0 Ω）接 VREF+；EX_BAT 与 3V3 经 Q1（BAT54C）接 VBAT，保持板上原状。

背面 SWD 从照片左到右为 RST、SWCLK、SWDIO、GND、3V3。PB8 标注 BOOT0，本方案未使用该脚；不要随意给启动相关脚加上下拉。PA9/PA10 的 TX1/RX1 丝印表示 USART1 复用能力，当前工程将它们配置为 TIM1 PWM，调试串口仍走 PB10/PB11。

PA13/PA14 用于顶部 SWD 的 DIO/CLK；PA11/PA12 对应原生 USB；PC14/PC15 对应低速晶振，这些接口保留。Flash、OLED/TFT、LED、K1/K2 的 GPIO 网络已按下表核实。显示屏 J3 不接外设。

## 驱动板接口

驱动板到 MCU 的唯一接线表见 [01](01-hardware-and-wiring.md)。下文只给 MCU 引脚到开发板排针的物理定位，不重复 CN3 表。

## 板载网络核验（2026-11-16）

依据 `STM32G474R开发板-原理图--202508.pdf` 第 1 页；第 2 页为不带二维码的同类电路图，第 3 页为布局与尺寸图。图纸标题 SN:202408，实物记录为 Ver.20；核对对象是所提供图纸，装配差异仍应以实物为准。

| 板载器件 / 接口 | 实际连接 | 对本方案的影响 |
|---|---|---|
| W25Q16 Flash U3 | PA15 CS、PC10 SCK、PC11 MISO、PC12 MOSI | 与所选 FOC 引脚不冲突；这些脚保留给 Flash |
| LED D1 / D2 | PC13 / PD2，各经 510 Ω 接 LED；低电平点亮 | 不冲突；电源灯 P 不占 GPIO |
| 按键 K1 / K2 | PA0 / PA1，按下接地 | 不冲突 |
| OLED/TFT J3 | 3:PB15、4:PB13、5:PB12、6:PB14、7:PC6、8:PC7 | 与三路下管 PWM、VBUS、两路霍尔并联，**J3 必须空置** |
| USB Type-C | PA11/PA12 经 R4/R5（0 Ω）接 USB D−/D+ | 保留；没有板载 USB 转串口芯片 |
| SWD J2 | 1:3V3、2:GND、3:PA13、4:PA14、5:NRST | 保留给调试器 |
| 晶振 | PF0/PF1 接 8 MHz；PC14/PC15 接 32.768 kHz | 保留；当前固件仍使用 HSI+PLL |
| BOOT0 | PB8 经 R6（5.1 kΩ）下拉 | 本方案不用 |

J3 的六根信号线在图纸中直接连 MCU 与排针，没有画额外缓冲或上下拉负载；不插显示模块时可用于本方案。PC0/PC1/PB1、PC8/PC9、PA8/PA9/PA10、PB10/PB11 未见额外板载负载。

为避免正反面混淆，下面补充原理图连接器脚号；`J4-13` 表示 J4 的第 13 脚，不是第 13 排。

| 信号 | MCU 引脚 | 原理图排针 |
|---|---|---|
| UH / VH / WH | PA8 / PA9 / PA10 | J4-13 / J4-16 / J4-15 |
| UL / VL / WL | PB13 / PB14 / PB15 | J4-8 / J4-7 / J4-10 |
| CTRL_SD | PC9 | J4-14 |
| HALL U / V / W | PC6 / PC7 / PC8 | J4-9 / J4-12 / J4-11 |
| AMP_IU / IV / IW | PC0 / PC1 / PB1 | J5-25 / J5-24 / J5-10 |
| VBUS | PB12 | J4-6 |
| USART3 TX / RX | PB10 / PB11 | J5-7 / J5-8 |

DS12288 Rev 6 的 Table 12（第 59、61、63 页）确认 PC0=ADC12_IN6、PC1=ADC12_IN7、PB1=ADC3_IN1/ADC1_IN12、PB12=ADC4_IN3/ADC1_IN11，因此当前 ADC1/2/3/4 分配成立。Table 13（第 73～75 页）确认 PA8/9/10 的 TIM1_CH1/2/3 为 AF6，PB13/14 的 TIM1_CH1N/2N 为 AF6，**PB15 的 TIM1_CH3N 为 AF4**；PB10/11 的 USART3_TX/RX 为 AF7。霍尔使用 GPIO/EXTI，不需要定时器复用。

注意两处名称相同的 VBUS：**开发板原理图的 VBUS 是 USB 5 V 电源网；驱动板 CN3-8 的 VBUS 是母线分压信号，接 PB12（J4-6）**，二者不能相接，更不能把电机母线原电压接到 PB12。

## 调试供电与 ADC 参考

用户已确认其 J-Link 的 3V 端可以输出电源。给开发板单独供电时，输出接 3V3（例如 J2-1 或 J5-1/2），地接 GND；SWDIO 接 J2-3、SWCLK 接 J2-4，NRST 可接 J2-5。需确保实际输出电压和电流满足板卡需求，并断开 Type-C 与外部 5 V 供电。标准 VTref 检测端不能替代电源输出。

原理图上 3V3 与 U2（RT9193-33GB）输出直接连接，没有画电源隔离；从 3V3 外供会同时施加到稳压器输出端，其反向供电耐受尚未由该器件手册确认，图纸不能证明所有供电组合均安全。开发板电源不用于给电机功率级供电。

由于 R11/R10 将 3V3 连到 VDDA/VREF+，ADC 参考会跟随供电。若实测供电是 3.0 V，若仍按固定 3.3 V 换算，则换算出的 ADC 输入电压约高 10%，相电流的偏置扣除也会受影响；现在由 ADC4 测 VREFINT 补偿；诊断时应与万用表核对 VREF+。模拟输入须处于实际 VREF+/VDDA 允许范围，不能按数字引脚的耐压能力判断 ADC 输入范围。

## 资料入口

原始开发板原理图、DS12288、RM0440 及驱动板手册保存在本地只读 `datasheet/`。具体固件配置以 `.ioc` 和源码为准，见 [生成配置](04-cubemx-configuration.md)。图纸核验不代表所有实物装配相同，接线前仍须核对排针丝印与方向。
