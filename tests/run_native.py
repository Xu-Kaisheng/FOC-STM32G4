"""在 Windows MSVC 主机上编译实际模块与 HAL 替身，运行软件回归测试。"""
from pathlib import Path
import os
import subprocess

# 与 MDK 工程的包含路径保持一致：源码只写 "xxx.h"，由 -I 决定位置。
BSP_INCLUDES = [
    "src/bsp/board",
    "src/bsp/pwm",
    "src/bsp/hall",
    "src/bsp/adc_cur",
    "src/bsp/adc_vbus",
    "src/bsp/uart",
    "src/bsp/time",
]
APP_INCLUDES = [
    "src/app/app",
    "src/app/cmd",
    "src/app/status",
    "src/app/perf",
    "src/app/log",
    "src/app/control",
    "src/algorithm/sixstep",
    "src/algorithm/foc",
]


def main():
    """发现现有 MSVC 工具链并执行测试，不修改 Keil 或原厂 SDK。"""
    root = Path(__file__).resolve().parents[1]
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        text=True).strip()
    if not installation:
        raise SystemExit("需要安装 MSVC C/C++ 构建工具，未找到可用工具链。")
    build = root / "build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"

    def includes(paths):
        return " ".join(f"/I{p}" for p in paths)

    # 运行时回归：除 board.c 外的全部实际模块；板级参数由测试自带桩替代。
    files = [
        "tests/test_runtime.c",
        "Core/Src/stm32g4xx_it.c",
        "src/bsp/adc_cur/adc_cur.c",
        "src/bsp/adc_vbus/adc_vbus.c",
        "src/bsp/pwm/pwm.c",
        "src/bsp/pwm/pwm_io.c",
        "src/bsp/pwm/pwm_duty.c",
        "src/bsp/pwm/pwm_bridge.c",
        "src/bsp/hall/hall.c",
        "src/bsp/uart/uart.c",
        "src/app/app/app_state.c",
        "src/app/app/app.c",
        "src/app/cmd/app_cmd.c",
        "src/app/status/app_status.c",
        "src/app/perf/perf.c",
        "src/app/perf/perf_print.c",
        "src/app/log/app_log.c",
        "src/app/control/app_control.c",
        "src/algorithm/sixstep/alg_sixstep.c",
        "tests/perf_clock.c",
    ]
    script = build / "run.cmd"
    # 只使用工作区固定路径生成批处理，编译输出均留在已忽略的 build 目录。
    script.write_text(
        f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
        'cl /nologo /utf-8 /std:c11 /W4 /WX /Itests/stubs /ICore/Inc '
        + includes(BSP_INCLUDES) + ' ' + includes(APP_INCLUDES) + ' '
        + ' '.join(files) + ' /Fobuild/native-tests/ /Febuild/native-tests/runtime.exe\n'
        'if errorlevel 1 exit /b 1\nbuild\\native-tests\\runtime.exe\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=root, check=True)

    g474_build = build / "g474"
    g474_build.mkdir(exist_ok=True)
    g474_script = g474_build / "run.cmd"
    g474_script.write_text(
        f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
        'cl /nologo /utf-8 /std:c11 /W4 /WX /Itests/stubs '
        + includes(["src/bsp/adc_cur", "src/bsp/adc_vbus", "src/bsp/board"]) + ' '
        'tests/test_g474.c src/bsp/adc_cur/adc_cur.c '
        'src/bsp/adc_vbus/adc_vbus.c src/bsp/board/board.c '
        '/Fobuild/native-tests/g474/ /Febuild/native-tests/g474/runtime.exe\n'
        'if errorlevel 1 exit /b 1\nbuild\\native-tests\\g474\\runtime.exe\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(g474_script)], cwd=root, check=True)

    perf_build = build / "perf"
    perf_build.mkdir(exist_ok=True)
    perf_script = perf_build / "run.cmd"
    perf_script.write_text(
        f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
        'cl /nologo /utf-8 /std:c11 /W4 /WX /Itests/stubs '
        + includes(["src/app/perf", "src/app/log", "src/bsp/time", "src/bsp/uart"]) + ' '
        'tests/test_perf.c tests/perf_clock.c '
        'src/app/perf/perf.c src/app/perf/perf_print.c '
        '/Fobuild/native-tests/perf/ /Febuild/native-tests/perf/runtime.exe\n'
        'if errorlevel 1 exit /b 1\nbuild\\native-tests\\perf\\runtime.exe\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(perf_script)], cwd=root, check=True)


if __name__ == "__main__":
    main()
