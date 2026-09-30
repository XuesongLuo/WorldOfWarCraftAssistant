# World of Warcraft AI Assistant

面向《魔兽世界》正式服玩家的信息辅助工具。项目通过 WoW 原生 Lua 插件提供视觉锚点，由 Windows C++ 伴侣程序承载透明浮层和截图，经受限的 TypeScript Codex Host 提供问答能力。

本项目不是战斗辅助或自动化工具：不读取游戏内存、不注入、不分析封包、不模拟输入，也不提供实时战斗指令。完整范围见 [产品需求](./WorldOfWarcraftAssistant-PRD.md) 与 [技术设计](./WorldOfWarcraftAssistant-Technical-Design.md)。

## 当前阶段

当前里程碑为 M0 技术可行性 PoC。顺序执行入口与验证证据见 [开发进度](./WorldOfWarcraftAssistant-Development-Progress.md)。

## 目录

- `addon/WowAIAssistant/`：WoW Lua 插件（STEP-005 实现）。
- `companion/`：C++20 Windows 伴侣程序最小外壳、后续 WebView2 前端和 C++ 测试。
- `codex-host/`：TypeScript Codex Sidecar（STEP-009 实现）。
- `codex/`：应用独立的 Codex 配置、技能和只读知识工具。
- `contracts/`：跨进程 JSON Schema（STEP-004 实现）。
- `docs/`：ADR、环境、风险、测试与发布证据。
- `scripts/`：项目本地开发和验证入口。

## 开发环境

Windows 系统级要求：Visual Studio 2022 Build Tools、MSVC v143 和 Windows 11 SDK。项目本地工具放在未跟踪的 `.tools/`；不要依赖最终用户预装开发工具。

当前精确开发基线记录在 `eng/toolchain.json` 与 `eng/bootstrap-lock.json`。Node/npm、vcpkg 和 7-Zip 均引导到未跟踪的项目 `.tools/` 目录；下载归档必须通过锁文件中的 SHA-256/SHA-512 校验。

首次检出先执行：

```powershell
.\scripts\Bootstrap-Dependencies.ps1
.\scripts\Invoke-Npm.ps1 ci
```

如果使用项目本地 Build Tools，先在当前 PowerShell 会话导入工具路径：

```powershell
. .\scripts\Enter-DevShell.ps1
```

随后执行：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
cmake --build --preset windows-msvc-debug --target verify
```

如果宿主终端同时注入了大小写不同的 `Path`/`PATH`（Codex Desktop 的某些环境会如此），旧版 MSBuild 会拒绝启动编译器。此时用项目包装器执行同样参数：

```powershell
.\scripts\Invoke-CMake.ps1 --preset windows-msvc-debug
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-debug
```

首次 CMake 配置会通过固定 baseline 的 `vcpkg.json` 恢复 Catch2、WIL、nlohmann-json、SQLite 和 spdlog。不要使用全局 npm 或全局 vcpkg 填补项目依赖。

Debug 伴侣程序构建后位于
`out/build/windows-msvc-debug/companion/Debug/wowai_companion.exe`。启动后显示等待 WoW
的最小窗口并创建系统托盘图标；再次启动会聚焦现有窗口。右键托盘图标选择 `Exit`，或关闭
主窗口，均走正常退出路径。重复启动与退出验收可执行：

```powershell
.\scripts\Test-Step006Companion.ps1 `
  -Executable '.\out\build\windows-msvc-debug\companion\Debug\wowai_companion.exe' `
  -Cycles 25
```

资源所有权约定见 `docs/architecture/companion-resource-ownership.md`。

完整 Node/TypeScript 验证：

```powershell
. .\scripts\Enter-DevShell.ps1
npm ci
npm run format:check
npm run lint
npm run typecheck
npm test
npm run build
.\scripts\Test-Dependencies.ps1
```

WoW 插件位于 `addon/WowAIAssistant/`。开发期静态验证执行：

```powershell
.\scripts\Test-WowAddon.ps1
```

人工验证时将整个 `WowAIAssistant` 目录复制到正式服
`_retail_/Interface/AddOns/`，不要只复制其中的 Lua 文件。当前 PoC 使用 `/wowai`
打开或关闭面板；伴侣程序尚未运行时会保持离线占位，不会影响插件加载。正式服验收步骤见
`docs/test-plans/STEP-005-wow-addon-manual-test.md`。

## 安全提示

不要提交真实 API 密钥、访问令牌、玩家截图、聊天正文、本地 Codex 配置或构建工具。截图只能由玩家主动触发并确认，Codex 的命令、文件修改、计算机控制和未知工具请求必须失败关闭。
