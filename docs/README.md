# Documentation / 文档目录

[English README](../README.md) | [中文 README](../README.zh-CN.md)

## 阅读与维护入口 / Reading and ownership

按任务进入对应主题。每个主题只维护自己的事实，其他页面使用链接；下表本身不复制当前功能状态。
Choose the topic for your task. Each topic owns its facts; other pages link to it instead of copying them.

| 入口 / Topic | English | 简体中文 | 唯一维护内容 / Owned facts |
|---|---|---|---|
| 01 | [Hardware and wiring](en/01-hardware-and-wiring.md) | [硬件与接线](zh-CN/01-hardware-and-wiring.md) | CN2/CN3、驱动电路、电机规格 / wiring and circuit facts |
| 11 | [Board reference](en/11-g474-board-guide.md) | [开发板参考](zh-CN/11-g474-board-guide.md) | 排针方位、共享网络、供电 / headers, shared nets, power |
| 04 | [Generated configuration](en/04-cubemx-configuration.md) | [生成配置](zh-CN/04-cubemx-configuration.md) | 配置来源及重新生成 / configuration sources and regeneration |
| 09 | [Operation and validation](en/09-validation-and-maintenance.md) | [操作与验证](zh-CN/09-validation-and-maintenance.md) | 当前能力、命令、默认参数、构建及试机 / capabilities, commands, defaults, build, operation |
| 07 | [Architecture](en/07-architecture-and-flow.md) | [代码架构](zh-CN/07-architecture-and-flow.md) | 职责、依赖和应用编排 / responsibilities and orchestration |
| 06 | [Module contracts](en/06-driver-modules.md) | [模块契约](zh-CN/06-driver-modules.md) | PWM、电流、霍尔、串口、六步 / PWM, current, Hall, UART, six-step |
| 10 | [Bus voltage and VREF](en/10-bus-voltage-adc.md) | [母线与参考电压](zh-CN/10-bus-voltage-adc.md) | ADC4 接口、换算及失效语义 / ADC4 conversion and failure semantics |
| 12 | [Performance accounting](en/12-performance-monitoring.md) | [性能统计](zh-CN/12-performance-monitoring.md) | 口径、接口及恢复 / accounting, interfaces, recovery |
| 02 | [FOC fundamentals](en/02-foc-fundamentals.md) | [FOC 原理](zh-CN/02-foc-fundamentals.md) | 理论 / theory |
| 03 | [Control roadmap](en/03-optimization-roadmap.md) | [控制路线](zh-CN/03-optimization-roadmap.md) | 未来方案及验收条件 / future work and acceptance |
| 05 | [Runtime design](en/05-runtime-design.md) | [运行时设计](zh-CN/05-runtime-design.md) | 控制频率及滤波设计 / loop rates and filtering design |

## 变更规则 / Change rules

- 硬件参考：01、11；配置工作流：04；使用与验收：09；实现说明：06、07、10、12；理论与规划：02、03、05。
- 普通变更只修改归属主题的中英文两份。代码/`.ioc` 是参数的实际来源；README 不保存参数或能力副本。
- 只有目录/导航变化才修改本页，只有开发约束变化才修改 AGENTS.md。
- 审查问题、开发过程、临时日志在会话或用户指定的 ITO 中保存。原 08 审查记录和 13 重构方案不再作为正式文档；长期规则归 AGENTS，稳定架构归 07。
- 不按模块文件数量增加文章。接口变更只改模块契约；影响用户操作时再改 09；单纯内部修复不要求更新所有文档。
- 保留既有主题编号，避免无关路径迁移。源码路径从仓库根目录解释，Markdown 链接从当前文档解析。

Update only the affected topic and its translation. Configuration lives in source/`.ioc`; README is navigation only. Update this index only for navigation changes and AGENTS only for development-rule changes. Keep investigation notes in the conversation or requested ITO inbox. Review history (former 08) and migration narrative (former 13) are not maintained manuals. Existing topic numbers remain stable. Design examples are not claims of implemented or measured behavior.
