# Windows 一键仿真交付

## 最终形态

客户安装包包含 QGC 和一个最小化的 Windows SITL 运行包：

```text
bin/
├── AeroFollow.exe
└── simulator/
    ├── bin/
    │   ├── arduplane.exe
    │   ├── cygwin1.dll
    │   └── 依赖扫描得到的其他 Cygwin DLL
    ├── params/
    │   ├── quadplane.parm
    │   └── aerofollow.parm
    ├── licenses/
    ├── manifest.json
    └── README.txt
```

客户机只需受支持的 64 位 Windows。客户不需要安装 WSL、Ubuntu、Cygwin、
Python、MAVProxy、编译器或 ArduPilot 源码，也不会在客户机执行 `waf build`。

目标 NMEA 模拟已从 `simulate_rtk_nmea_udp.py` 移入 QGC 的 C++/Qt 进程，因而
不再有 Python 运行时依赖。飞机仍然是真实的 ArduPlane SITL 固件逻辑，但使用
开发机预先编译的 `arduplane.exe`。

## 开发机构建运行包

开发机需要完整 Cygwin 构建环境和与交付版本匹配的 ArduPilot 源码。执行：

```powershell
cd D:\work\开源飞控\QGC-V5.0.8-REVISE\qgroundcontrol
.\tools\package-ardupilot-sitl-windows.ps1 -Action all `
    -ArduPilotSource D:\src\ardupilot `
    -CygwinRoot C:\cygwin64
```

`-Action all` 只编译 ArduPlane，然后递归读取 PE 导入表，收集实际需要的
Cygwin DLL，生成 SHA-256 清单并执行启动冒烟测试。默认输出为：

```text
artifacts\sitl-windows
```

已有 Cygwin 版 `arduplane.exe` 时，可以跳过编译：

```powershell
.\tools\package-ardupilot-sitl-windows.ps1 -Action package `
    -ArduPilotSource D:\src\ardupilot `
    -CygwinRoot C:\cygwin64 `
    -PlaneExecutable D:\src\ardupilot\build\sitl\bin\arduplane.exe
```

源码参数仍用于取得匹配的 `quadplane.parm`、许可证和 Git 修订号，不会进入
客户运行包。

## 构建 QGC 和安装包

将运行包传给 QGC 构建脚本：

```powershell
.\tools\build-qgc-windows.ps1 -Action all -Config Release `
    -SitlPackage .\artifacts\sitl-windows
```

脚本会把运行包放在 `AeroFollow.exe` 同目录的 `simulator` 子目录，同时将路径
写入 CMake 安装规则。随后执行 CMake install/NSIS 制作安装程序时，该目录会
进入安装包。未传 `-SitlPackage` 时会明确警告，QGC 的环境检查也会报告缺失文件。

## 客户机验收

1. 安装 QGC，不安装任何额外开发环境。
2. 打开“应用设置 -> 仿真训练”，点击“检查运行环境”。
3. 确认日志明确显示“不需要 WSL、ArduPilot 源码、Python 或编译工具”。
4. 点击启动，确认 QGC 通过 UDP 14550 连接 ArduPlane，并持续接收目标位置。
5. 停止仿真和退出 QGC，确认任务管理器中没有遗留 `arduplane.exe`。

SITL 的 EEPROM、日志和地形数据写入 `%LOCALAPPDATA%` 下的 QGC 数据目录，
不会尝试写入 `Program Files`。

## 许可证要求

运行时不需要携带完整源码，但分发义务不能省略。ArduPilot 是 GPLv3 软件，
交付方必须针对所交付的精确二进制提供对应源码，或按 GPLv3 提供有效的书面
源码要约。源码可以作为独立下载或单独介质提供，不需要安装到客户电脑。

每个交付版本至少归档以下内容：ArduPilot Git 提交号、构建补丁、构建命令、
运行包 `manifest.json`、许可证文本，以及对应源码的获取方式。Cygwin DLL 的
许可证和对应源码义务也必须一并核对。此项应由交付方的合规负责人最终确认。
