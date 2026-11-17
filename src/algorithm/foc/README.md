# FOC 算法落点（预留）

本目录放**无硬件依赖**的 FOC 数学与算法：Clarke、Park、逆 Park、PI、
SVPWM 扇区计算、角度插值等。**当前版本不实现**。

## 约束

- 只允许 `#include <stdint.h>/<stddef.h>/<math.h>` 与自己的头文件；
- 不得引用 `bsp/`、`app/` 或任何寄存器/HAL 符号；
- 需要采样或写 PWM 时，由调用方（`app/control`）通过回调注入；
- 纯函数优先：输入角度/电流/给定，输出三相占空比或电压矢量。

## 与 app/control 的分工

- `algorithm/foc`：算"输出什么"；
- `app/control`：决定"什么时候算、用哪组参数、把输出写给哪个 BSP 接口"。
