# World of Warcraft AI Assistant

面向《魔兽世界》正式服玩家的信息辅助工具。项目通过 WoW 原生 Lua 插件提供视觉锚点，由 Windows C++ 伴侣程序承载透明浮层和截图，经受限的 TypeScript Codex Host 提供问答能力。

本项目不是战斗辅助或自动化工具：不读取游戏内存、不注入、不分析封包、不模拟输入，也不提供实时战斗指令。完整范围见 [产品需求](./WorldOfWarcraftAssistant-PRD.md) 与 [技术设计](./WorldOfWarcraftAssistant-Technical-Design.md)。

## 当前阶段

当前里程碑为 M0 技术可行性 PoC。顺序执行入口与验证证据见 [开发进度](./WorldOfWarcraftAssistant-Development-Progress.md)。

## 目录

- `addon/WowAIAssistant/`：WoW Lua 插件（STEP-005 实现）。
- `companion/`：C++20 Windows 伴侣程序、WebView2 前端和 C++ 测试。
- `codex-host/`：TypeScript Codex Sidecar（STEP-009 实现）。
- `codex/`：应用独立的 Codex 配置、技能和只读知识工具。
- `contracts/`：跨进程 JSON Schema（STEP-004 实现）。
- `docs/`：ADR、环境、风险、测试与发布证据。
- `scripts/`：项目本地开发和验证入口。

## 开发环境

Windows 系统级要求：Visual Studio 2022 Build Tools、MSVC v143 和 Windows 11 SDK。项目本地工具放在未跟踪的 `.tools/`；不要依赖最终用户预装开发工具。

当前精确开发基线记录在 `eng/toolchain.json`。Node/npm 的项目级分发与锁文件在 STEP-003 完成；在那之前，Node 版本只表示目标基线，不代表发布运行时已经就绪。

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

首次 CMake 配置会下载已锁定 URL 与 SHA-256 的 Catch2 v3.8.1 到构建缓存。STEP-003 会把正式 C++ 依赖迁移到固定 baseline 的 vcpkg manifest。

TypeScript/npm 工作区和锁文件在 STEP-003 建立；在此之前不要使用全局 npm 包填补依赖。

## 安全提示

不要提交真实 API 密钥、访问令牌、玩家截图、聊天正文、本地 Codex 配置或构建工具。截图只能由玩家主动触发并确认，Codex 的命令、文件修改、计算机控制和未知工具请求必须失败关闭。
