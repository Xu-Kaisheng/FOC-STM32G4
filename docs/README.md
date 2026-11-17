# Documentation / 文档目录

[English README](../README.md) | [中文 README](../README.zh-CN.md)

English documentation is in `en/`; Chinese documentation is in `zh-CN/`. Matching topics use identical numbers and filenames, with language links at the top of every page. Source paths shown as code are relative to the repository root unless stated otherwise.

英文文档位于 `en/`，中文文档位于 `zh-CN/`。同一主题使用相同编号和文件名，页首可切换语言。正文中以代码形式显示的源码路径，除特别说明外均相对于仓库根目录。

The firmware currently provides bridge-off diagnostics. The user reported 990 ns dead time in the temporary PWM test; the demo is disabled. Synchronized current sampling, FOC current/speed loops, and fast 8 A limiting remain pending. Design articles and estimated timing are not implementation or measurement claims.

当前固件处于关桥诊断阶段；用户反馈临时 PWM 测试正常、死区实测 990 ns，demo 已关闭。三相同步采样、FOC 电流/速度环和 8 A 快速限流仍待实现；设计文档和估算时序不代表已经实现或实测。

| # | English | 简体中文 | Scope / 范围 |
|---|---|---|---|
| 01 | [Hardware and wiring](en/01-hardware-and-wiring.md) | [硬件架构与接线](zh-CN/01-hardware-and-wiring.md) | Implemented / 已实现与接线 |
| 02 | [FOC fundamentals](en/02-foc-fundamentals.md) | [FOC 原理与数据流](zh-CN/02-foc-fundamentals.md) | Design / 后续设计 |
| 03 | [Optimization roadmap](en/03-optimization-roadmap.md) | [技术优化路线](zh-CN/03-optimization-roadmap.md) | Roadmap / 规划 |
| 04 | [CubeMX configuration](en/04-cubemx-configuration.md) | [CubeMX 配置](zh-CN/04-cubemx-configuration.md) | Implemented / 当前配置 |
| 05 | [Runtime design](en/05-runtime-design.md) | [运行时框架](zh-CN/05-runtime-design.md) | Current monitoring + planned control / 统计已实现、控制待实现 |
| 06 | [Driver modules](en/06-driver-modules.md) | [驱动模块](zh-CN/06-driver-modules.md) | Implemented / 已实现 |
| 07 | [Architecture and flow](en/07-architecture-and-flow.md) | [代码架构与流程](zh-CN/07-architecture-and-flow.md) | Implemented + locked six-step path / 当前实现与受限路径 |
| 08 | [Review findings](en/08-review-findings.md) | [审查问题核对](zh-CN/08-review-findings.md) | Review / 审查记录 |
| 09 | [Validation and maintenance](en/09-validation-and-maintenance.md) | [验证与维护](zh-CN/09-validation-and-maintenance.md) | Validation / 验证记录 |
| 10 | [ADC4 bus-voltage monitoring](en/10-bus-voltage-adc.md) | [ADC4 母线电压采集](zh-CN/10-bus-voltage-adc.md) | Monitoring only / 仅监视 |
| 11 | [G474 board guide](en/11-g474-board-guide.md) | [G474 工程与接线核验](zh-CN/11-g474-board-guide.md) | Board and measured PWM / 板卡及 PWM 实测 |
| 12 | [Performance monitoring](en/12-performance-monitoring.md) | [控制耗时与 CPU 负载统计](zh-CN/12-performance-monitoring.md) | Implemented / 已实现 |
| 13 | [Repository restructure](en/13-repo-restructure.md) | [仓库结构重构](zh-CN/13-repo-restructure.md) | Implemented / 已实施 |

## Maintaining translations / 双语维护

Update both language versions when behavior, wiring, parameters, or validation status changes. Keep formulas, pin assignments, examples, and current limitations consistent. Repository code comments and commit messages remain Chinese. Vendor SDK documents and licenses retain their original language and content. Local `datasheet/` references are not shipped with the repository.

修改行为、接线、参数或验证状态时同步更新中英文，保持公式、引脚、示例及限制一致。代码注释和提交说明仍使用中文；原厂 SDK 文档与许可证保持原文。`datasheet/` 是本地参考资料，不随仓库发布。
