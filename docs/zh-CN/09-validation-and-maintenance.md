# 09 操作、构建与验证

[English](../en/09-validation-and-maintenance.md) | **简体中文** | [文档目录](../README.md)

本页是当前能力、启动许可、命令与操作参数的唯一说明入口。接线见 [01](01-hardware-and-wiring.md)，模块契约见 [06](06-driver-modules.md)。

## 当前能力与边界

- 上电为 STOP，CTRL_SD 保持低，PWM 自检关闭。`1`/`2` 通过 ADC/PWM 联锁后启动开环六步；只有明确启动命令才开桥。
- 六步按设定电频率定时推进，霍尔只用于监视，不根据转子位置换相。没有自动对齐、加速斜坡、电流环或速度环，不保证低速平稳跟随。
- 电流依次软件采样，母线/VREF 独立采集；每 500 ms 输出状态。参考电压补偿见 [10](10-bus-voltage-adc.md)，性能统计见 [12](12-performance-monitoring.md)。
- 8 A 软件限流按维护者要求待优化；没有过欠压、堵转及失步保护。低频电流打印和占空比限幅均不能代替限流。
- ADC 初始化、采集或样本时效失败后关桥并锁住启动，须复位；PWM 联锁失败退回 STOP。更改模式先关桥停 PWM，并等待 200 ms 再继续。

## 串口操作

串口参数与接线见 [01](01-hardware-and-wiring.md)。命令单字符有效，不需要回车。

| 命令 | 行为 |
|---|---|
| `0` | 停机、关桥 |
| `1` / `2` | 正向 / 反向六步请求；实际轴端方向须观察确认 |
| `h` | 关桥，进入霍尔变化监视 |
| `+` / `-` | 占空比增减 5 个百分点 |
| `>` / `<` | 电频率乘以 / 除以 1.5 |
| `?` | 帮助 |

| 操作参数 | 当前值 | 源码 |
|---|---|---|
| 初始电频率 / 调节范围 | 1 Hz / 0.5～20 Hz | `src/app/control/app_control.c` |
| 初始占空比 / 调节范围 | 20% / 5%～60% | 同上 |
| 模式切换关桥等待 | 200 ms | `src/app/app/app.c` |
| 命令调节步长 | 5 个百分点 / 频率倍率 1.5 | `src/app/cmd/app_cmd.c` |

`1`/`2` 不重置频率与占空比；复位恢复初始值。每电周期六次换相，间隔约 `1000/(6×f)` ms，存在毫秒取整及主循环抖动。电机为两对极，同步跟随时 `rpm=60×f/2`，默认约 30 rpm；日志 f 是设定值，不是测速。500 ms 霍尔快照可能跳过中间状态，不能据其交替直接断言反转。

## 构建与下载

唯一工程 `MDK-ARM/G474-FOC.uvprojx`；`STM32-FOC.uvmpw` 是快捷入口。

1. 从 PATH 查找 `UV4.exe`；Windows 也可用 `Get-Command UV4.exe`。未找到时查询 `HKLM\SOFTWARE\Keil\Products\MDK` 或 32 位视图 `HKLM\SOFTWARE\WOW6432Node\Keil\Products\MDK` 的 Path，再定位 UV4 并核实文件存在。
2. 仅在当前终端设置 `KEIL_UV4`。工具链/设备包要求从 `.ioc`、`.uvprojx` 读取；不修改编译选项适配个人环境，不提交本机路径或安装清单。
3. Windows 在仓库根目录 Git Bash 执行：

```bash
: "${KEIL_UV4:=$(command -v UV4.exe || true)}"
: "${KEIL_UV4:?Locate UV4.exe and set KEIL_UV4 for this shell first}"
"$KEIL_UV4" -r "MDK-ARM/G474-FOC.uvprojx" -j0 -o "../build-g474.log"
python -X utf8 tests/run_native.py
bash tests/check_structure.sh
```

Keil 日志路径相对工程目录，`../` 将日志置于仓库根目录。检查新日志的 `0 Error(s)` 并解释 warning。产物为 `MDK-ARM/G474-FOC/G474-FOC.axf|.hex`，日志和产物由 Git 忽略。

下载前核对目标 MCU、实际探针与下载配置，确认功率级处于可控停机状态。用户授权后使用已有 Keil 下载配置：

```bash
"$KEIL_UV4" -f "MDK-ARM/G474-FOC.uvprojx" -j0 -o "../build-flash.log"
```

检查 `Verify OK`、程序启动与串口日志；编译成功不等于烧录成功，烧录成功不等于转动验收。抓串口的工具退出后释放端口。

## 回归与实板验证

| 测试 | 覆盖 |
|---|---|
| test_runtime | PWM/COM 联锁、上电不输出、启停、串口和 ADC 故障锁存 |
| test_g474 | 实际板级许可、ADC 换算/完整性、VREFINT |
| test_perf | 工作并集、嵌套中断、回绕、DWT 失效恢复 |

主机寄存器模型不能验证死区、电气极性或真实模拟采样。修改哪个模块，就执行对应硬件验证；方法分别放在 [PWM/电流/霍尔/串口](06-driver-modules.md)、[母线/VREF](10-bus-voltage-adc.md)、[性能统计](12-performance-monitoring.md)。原始日志和临时实验放本地 build 或会话，不在手册追加逐轮报告。

首次空载试转：固定电机，电源设置硬件限流并准备断电。上电先保持 STOP，确认霍尔合法、静止电流输入和母线正常。逐次发送 `-`，看到 5% 后再短时发送 `1`，用 `0` 停机；完全停稳后再试 `2`。低占空比不起转不能直接认定相序错误，不要在堵转时持续加占空比。异常抖动、异响或电源限流先停机。记录转动方向是否连续、霍尔变化与换相时序，再决定调整；尚未验证的平稳跟随/带载能力不得宣称通过。
