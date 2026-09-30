# STEP-006 C++ 伴侣程序本地验证记录

- 日期：2026-09-30
- 平台：Windows 11 x64
- 工具链：MSVC 19.44.35229.0、Windows SDK 10.0.26100.0、CMake 3.31.6
- 配置：`windows-msvc-debug`、`windows-msvc-release`

## 覆盖范围

- Win32/C++20 GUI 入口与 STA COM 初始化。
- 会话内命名互斥体单实例，以及重复启动激活既有窗口。
- 系统托盘图标、托盘 `Exit` 和窗口关闭的统一退出路径。
- 生命周期状态机正常、非法、WoW 退出和致命错误路径。
- Win32 句柄异常展开与 WIL 所有权封装。

## 自动验证

```powershell
.\scripts\Invoke-CMake.ps1 --preset windows-msvc-debug
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-debug
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify

.\scripts\Invoke-CMake.ps1 --preset windows-msvc-release
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-release
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-release --target verify

.\scripts\Test-Step006Companion.ps1 `
  -Executable '.\out\build\windows-msvc-debug\companion\Debug\wowai_companion.exe' `
  -Cycles 25
```

结果：

- Debug 与 Release 均配置、编译成功，使用 `/W4 /permissive- /EHsc /utf-8`。
- Catch2 共 16 个测试全部通过；新增测试覆盖状态转换、重复获取命名互斥体、非法构造、
  COM apartment 和异常展开时的 Win32 句柄计数恢复。
- 统一 `verify` 门禁通过，包括仓库结构、STEP-001 文档、WoW 插件静态安全检查和
  `git diff --check`。
- 真实进程测试连续完成 25 轮“启动主实例 → 重复启动 → 既有窗口获得前台焦点 →
  窗口关闭/托盘退出命令交替正常退出”；重复进程退出码为 0，主进程未被替换，每轮关闭后
  均无残留伴侣进程。

## 结论

STEP-006 的实现与验证条件均满足。当前最小窗口仅展示 `WaitingForWow` 状态；WoW 窗口发现
与锚点识别属于 STEP-007，不在本步骤范围内。
