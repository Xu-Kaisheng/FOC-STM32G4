#!/bin/sh
# 仓库结构合规检查（对应 docs/zh-CN/13-repo-restructure.md 第 5.3 节）
# 返回非 0 表示存在违规；可在 Git Bash 或 CI 直接运行。
#
# 与文档原文的差异（均为文档自相矛盾处，已在第 13 篇"实施状态"说明）：
#   1) 业务标识符按实际命名匹配，避免把 HAL 的 TIM_COUNTERMODE_xxx 误报；
#   2) 分节横幅要求 ≥ 1 个：单一职责文件可能只有 write 或 task 一节。
set -u
fail=0
cd "$(dirname "$0")/.." || exit 1

# 1) bsp 反向依赖 app
if grep -rn '#include "\.\./app/' src/bsp/ ; then
    echo "FAIL: bsp 反向依赖 app"; fail=1
fi

# 2) bsp 模块间横向 include（board.h 除外）
if grep -rn '#include "\.\./\(pwm\|hall\|adc_cur\|adc_vbus\|uart\|time\)/' src/bsp/ ; then
    echo "FAIL: bsp 存在横向 include"; fail=1
fi

# 3) bsp 出现应用业务标识符
if grep -rn 'SIXSTEP\|APP_MODE_\|app_perf\|app_board\|app_sixstep' src/bsp/ ; then
    echo "FAIL: bsp 出现应用层标识符"; fail=1
fi

# 4) bsp 不得承载任务（主循环推进）：不允许出现 *_task()
if grep -rn '_task(' src/bsp/ ; then
    echo "FAIL: bsp 出现 task 函数"; fail=1
fi

# 5) 算法层/第三方不得依赖 app 或 bsp
for d in src/algorithm src/third_party ; do
    [ -d "$d" ] || continue
    if grep -rn '#include "\.\./\|#include "app_\|#include "bsp_\|#include "pwm\.h"\|#include "hall\.h"\|#include "adc_\|#include "uart\.h"\|#include "board\.h"\|#include "perf\.h"\|#include "main\.h"' "$d" ; then
        echo "FAIL: $d 反向依赖 app/bsp"; fail=1
    fi
done

# 6) app 每个子文件夹都必须提供 task 入口
for d in src/app/*/ ; do
    [ -d "$d" ] || continue
    if ! grep -rqE '[a-z_]+_task\(void\)' "$d" ; then
        echo "FAIL: $d 缺少 task 入口"; fail=1
    fi
done

# 7) 旧路径与已废除符号残留
if grep -rn 'src/drv/\|platform/\|bsp_perf\|app_board_\|app_sixstep' src/ Core/Src/ ; then
    echo "FAIL: 旧路径或旧符号残留"; fail=1
fi
if grep -n 'src/drv/\|platform/\|bsp_perf\.c' MDK-ARM/G474-FOC.uvprojx ; then
    echo "FAIL: 工程文件残留旧路径"; fail=1
fi

# 8) 单文件行数（软限 250，仅提示）
for f in $(find src -name '*.c'); do
    n=$(wc -l < "$f")
    [ "$n" -gt 250 ] && echo "WARN: $f 共 $n 行，超过 250 行软限"
done

# 9) 模块结构模板：分节横幅顺序必须为 init < task < read < write < hook
for f in $(find src -name '*.c'); do
    prev=0; sections=0
    for sec in init task read write hook; do
        ln=$(grep -n "^/\* -\{4,\} $sec -\{4,\} \*/$" "$f" | head -1 | cut -d: -f1)
        [ -z "$ln" ] && continue
        sections=$((sections + 1))
        if [ "$ln" -lt "$prev" ]; then
            echo "FAIL: $f 分节顺序错误（$sec 出现在前一节之前）"; fail=1
        fi
        prev=$ln
    done
    [ "$sections" -lt 1 ] && { echo "FAIL: $f 缺少分节横幅"; fail=1; }
done

exit $fail
