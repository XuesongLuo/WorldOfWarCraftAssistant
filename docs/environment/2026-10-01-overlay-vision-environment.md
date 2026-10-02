# 2026-10-01 覆盖层与画面感知开发环境复查

## 结论

现有 MSVC、Windows SDK、CMake、Node.js、npm 和 vcpkg 基线继续使用，无需整体升级。为透明覆盖层补充锁定的 WebView2 SDK；WebView2 Runtime 使用 Evergreen 策略，只记录检测结果和最低能力，不锁死最终用户机器上的精确版本。Direct3D 11、DXGI、DirectComposition、WIC、Windows Graphics Capture 和 C++/WinRT 均由现有 Windows SDK 提供，不新增旧版 DirectX SDK、OpenCV、ONNX Runtime、Python 或 CUDA。

## 已验证基线

| 项目 | 状态 | 版本或证据 |
|---|---|---|
| Windows 目标 | 可用 | Windows 11，最低构建基线 `10.0.26100.0` |
| Visual Studio Build Tools | 可用 | `17.14.37710.0` |
| MSVC | 可用 | 编译器 `19.44.35229.0`，Tools `14.44.35207` |
| Windows SDK | 可用 | `10.0.26100.0` |
| CMake | 可用 | `3.31.6-msvc6`，通过项目包装脚本调用 |
| Node.js / npm | 可用 | `24.19.0` / `11.17.0`，项目本地 `.tools/node` |
| vcpkg | 可用 | 项目锁定 commit 和工具版本 |
| WebView2 SDK | 已登记 | `Microsoft.Web.WebView2 1.0.4258.31`，官方 nupkg SHA-256 `56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9` |
| WebView2 Runtime | 已安装 | Evergreen Runtime `154.0.4258.37`；发布时仍需检测缺失并安全提示 |
| Direct3D/DXGI/DirectComposition | 可用 | Windows SDK 头文件及 x64 库存在 |
| Windows Graphics Capture | 可用 | `windows.graphics.capture.interop.h`、C++/WinRT 和 `windowsapp.lib` 存在 |
| WIC | 可用 | `wincodec.h` 存在 |

## 项目策略

1. WebView2 SDK 作为构建输入固定版本和哈希，恢复到 `.tools/webview2-sdk`。
2. WebView2 Runtime 使用 Evergreen 模式。开发、安装和启动路径必须检测 Runtime；缺失时不得静默失败。
3. STEP-008 使用 WebView2 Composition Controller 与 DirectComposition 验证透明、焦点、DPI 和多显示器行为；失败时按 ADR-014 回退 Direct2D/DirectWrite。
4. STEP-012 将现有 GDI 捕获 PoC 迁移到 Windows Graphics Capture。该 API 和 C++/WinRT 已由现有 SDK 满足。
5. 像素数据桥首版只需要 BGRA 帧扫描、CRC 和协议解码，不因此引入计算机视觉框架。
6. OCR 或本地场景分类器必须先经过独立 PoC 和 ADR；只有确定部署、许可、性能及隐私边界后才能增加 ONNX Runtime、Windows OCR 或其他依赖。

## 验证入口

```powershell
.\scripts\Bootstrap-Dependencies.ps1
.\scripts\Test-Dependencies.ps1 -RequireWebViewRuntime
.\scripts\Invoke-CMake.ps1 --version
.\scripts\Invoke-Npm.ps1 --version
```

普通 PowerShell 的 `PATH` 不保证直接包含 `cmake` 或 `ctest`；项目验证必须使用包装脚本，或先运行 `.\scripts\Enter-DevShell.ps1`。

## 与旧记录的关系

`2026-09-28-development-environment.md` 是 STEP-001 当时的历史快照，其中 npm、项目 vcpkg 和 WebView2 Runtime 状态已经过时，本文件是当前有效的覆盖层与画面感知环境基线。
