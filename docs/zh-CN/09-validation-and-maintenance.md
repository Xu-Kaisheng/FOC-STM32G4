# 09 验证与维护

[English](../en/09-validation-and-maintenance.md) | **简体中文** | [文档目录](../README.md)

当前仅维护 STM32G474RET6 / DevEBox Ver.20。固件保持关桥诊断。2026-11-17 用户反馈临时开机 PWM 测试正常、死区实测 990 ns，现已关闭 demo；电压、ADC 偏置与控制时序仍待验证。

## 软件验证入口

```bash
"C:/Keil_v5/UV4/UV4.exe" -r "E:/Code/STM32-FOC/MDK-ARM/G474-FOC.uvprojx" -j0 -o "E:/Code/STM32-FOC/build-g474.log"
python -X utf8 tests/run_native.py
```

Keil 使用 ARM Compiler 5.06 update 7、STM32G4xx_DFP 1.5.0。主机测试使用现有 MSVC 工具链，构建脚本通过 vswhere 定位，不安装或修改 SDK。

## 回归范围

| 测试 | 覆盖 |
|---|---|
| test_runtime | G474 参数下的 PWM 联锁、霍尔映射、串口队列、实际应用启动/停止与 ADC 失败锁存 |
| test_g474 | 实际平台 ADC1/2/3 完整样本发布、ADC4 母线、平台默认禁止驱动策略 |
| test_perf | 有效工作时间并集、嵌套中断、DWT 回绕、超时预算与日志 |

运行时测试允许通过测试板级替身临时打开驱动许可，以覆盖保留的六步路径和故障关断；实际 G474 平台默认许可仍为 0，由 test_g474 另行验证。主机寄存器替身不能替代实际硬件互补波形、死区与模拟转换验证。

## 实板验证顺序

1. 核对 J3 空置、串口 PB10/PB11、SWD 和地线；确认烧录目标为 STM32G474RET6。
2. 保持功率级关闭，确认启动日志为 MONITOR ONLY，命令 1/2 被拒绝，PC9 始终低。
3. 手转核对霍尔，测量三相输入与 VBUS 分压后接线，检查原始码和换算值。
4. 临时开机 PWM 测试已关闭；后续需另行启用关桥测试，先启动逻辑分析仪采集，再复位 MCU，核对 PA8/PB13、PA9/PB14、PA10/PB15 的 20 kHz 互补输出与 1 µs 双低死区。
5. 同步采样、电流环、8 A 快速保护实现后，再做同周期完整性、最坏耗时、故障锁存和带载验收。

## 2026-11-16 清理后验证结果

- 唯一 G474 目标重编译通过：0 Error(s)、0 Warning(s)。Code=28140、RO-data=1492、RW-data=104、ZI-data=4496 字节。
- 三组主机回归全部通过；运行测试已改用 G474 平台 ADC 实现、PC6/7/8 霍尔、PC9 关桥、ARR=4250、DTG=149。
- G474 的 104 个 Core、Drivers、CubeMX 配置和 Keil 工程文件与清理前 SHA-256 一致；仅清理平台相关内容，不改变实际固件控制策略。
- 工作区只有一个工程，所有 Keil 源文件路径及 Markdown 链接可解析；未烧录，未执行硬件实测。

## 2026-11-17 结构重构后烧录验证

- 重新编译：0 Error(s)、0 Warning(s)，Code=30904、RO-data=1488、RW-data=104、ZI-data=4608。
- J-Link 烧录：Erase/Program/Verify OK，目标电压 3.283 V，烧录后应用自动运行。
- 115200 8N1 串口（J-Link CDC UART）验证：启动日志 `[BOARD] G474RET6 DevEBox / MONITOR ONLY`、`STM32-FOC v0.1 boot`、`HALL=INV`；`?` 帮助正常；`1` 返回 `[LOCK] Board wiring not verified, monitor only`，桥保持关断。
- DMA 日志发送与命令接收均正常，长日志未出现丢字。
- 母线读取约 22.2~22.4 V（raw 1099~1113），换算链路可用。
- 三相电流原始值约 362/445/985，换算显示 -7.9/-7.4/-3.8 A，明显偏离 0 A 对应的 1.25 V（raw≈1551）。说明电流采样链路当前不在预期零点，**不得当作有效电流使用**；需先核对接线与偏置再开桥。
- 霍尔读数为 `INV`（000/111），符合电机/霍尔未接入或接线待核验的状态。
- 性能统计：J-Link 连接期间正常，CPU work≈55.8%、irq≈0.53 ms/s；J-Link 断开后固定显示 `[PERF] unavailable`，说明本板 DWT 计数依赖调试域供电。待优化项：核查 DBGMCU/调试时钟，或改用定时器时基做独立运行统计。

未执行：开桥带载、PWM 波形复测、同步电流采样与限流验证。以上结果不能替代功率级验收。
