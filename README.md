# World of Warcraft AI Assistant

面向《魔兽世界》正式服玩家的信息辅助与新手教学工具。Windows C++ 伴侣程序在游戏窗口上方承载透明交互浮层、实时聊天、技能输出教学和只读画面理解，经受限的 TypeScript Codex Host 提供问答能力；WoW 原生 Lua 插件是可选组件，可提供视觉锚点、公开上下文卡片和玩家明确开启的单向视觉数据桥。

本项目不是战斗辅助或自动化工具：不读取游戏内存、不注入、不分析封包、不模拟输入，也不提供实时战斗指令。完整范围见 [产品需求](./WorldOfWarcraftAssistant-PRD.md) 与 [技术设计](./WorldOfWarcraftAssistant-Technical-Design.md)。

## 当前阶段

当前里程碑为 M0 技术可行性 PoC。顺序执行入口与验证证据见 [开发进度](./WorldOfWarcraftAssistant-Development-Progress.md)。

## 目录

- `addon/WowAIAssistant/`：可选的 WoW Lua 插件；提供视觉锚点、公开上下文卡片、单向视觉数据桥和游戏内设置，不承载聊天，也不接收本地程序指令。
- `companion/`：C++20 Windows 伴侣程序最小外壳、后续 WebView2 前端和 C++ 测试。
- `codex-host/`：TypeScript Codex Sidecar（STEP-009 实现）。
- `codex/`：应用独立的 Codex 配置、技能和只读知识工具。
- `contracts/`：跨进程 JSON Schema（STEP-004 实现）。
- `docs/`：ADR、环境、风险、测试与发布证据。
- `scripts/`：项目本地开发和验证入口。

## 开发环境

Windows 系统级要求：Visual Studio 2022 Build Tools、MSVC v143 和 Windows 11 SDK。项目本地工具放在未跟踪的 `.tools/`；不要依赖最终用户预装开发工具。

当前精确开发基线记录在 `eng/toolchain.json` 与 `eng/bootstrap-lock.json`。Node/npm、vcpkg、7-Zip 和 WebView2 SDK 均引导到未跟踪的项目 `.tools/` 目录；下载归档必须通过锁文件中的 SHA-256/SHA-512 校验。WebView2 Runtime 使用 Evergreen 策略并在运行前检测，不锁定用户机器上的精确版本。

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

STEP-007 窗口与锚点 PoC 会通过可执行文件名和进程信息枚举 WoW 顶层窗口，不依赖易变的窗口
标题。多个客户端无法由已保存的稳定标识唯一匹配时，必须从托盘菜单明确选择；
`Forget saved WoW selection` 可清除该选择。打开插件的
`/wowai` 面板后选择 `Refresh WoW windows` 可执行一次只读客户区锚点检测；图像仅存在于
当前进程内存，不写入磁盘。自动检测失败时可使用托盘中的 `Manual calibration...`，并可随时
`Reset calibration`。正式服验收步骤见
`docs/test-plans/STEP-007-wow-window-anchor-manual-test.md`。

STEP-008 已加入独立的 DirectComposition + WebView2 透明聊天覆盖层。选中 WoW 后，覆盖层以
客户区右下角为无插件默认位置；插件锚点仅提供可选增强。覆盖层只在所选 WoW 或覆盖层自身
处于前台时显示，WoW 最小化、退出或切换到其他应用后自动隐藏。界面当前只返回明确标记的
本地确定性模拟回复，不连接 Codex 或网络。从托盘可切换 `Enable mouse pass-through` / 
`Enable overlay interaction`；人工验收见
`docs/test-plans/STEP-008-overlay-manual-test.md`。

STEP-009 已加入可独立打包的严格 TypeScript Host、确定性 `ICodexRuntime` 模拟实现，以及
C++ JSONL/stdio 客户端。C++ 使用 Windows Job Object 唯一拥有 Host 进程树，并覆盖分包、
粘包、乱码、超长、超时、崩溃和孤儿进程测试。Codex CLI 0.159.2 的 Windows x64 npm 来源、
完整性、开发二进制哈希和生成的 App Server v2 Schema 已锁定在
`eng/codex-runtime-lock.json`；Host 的配置、状态和空工作目录由应用专用目录助手创建，不读取
用户全局 Codex 配置。

STEP-010 已实现锁定 App Server 的真实 stdio 启动、initialize、thread/turn 生命周期、流式
文本归一化、取消和失败关闭审批策略。默认仍使用离线 mock；只有同时设置
`WOWAI_CODEX_BINARY`、`WOWAI_CODEX_LOCK` 和 `WOWAI_CODEX_ROOT` 才会启用真实 App Server。
当前生产工具注册表为空，所有命令、写入、权限、MCP、动态工具、计算机控制及未知事件均被
阻断。模型提供方和纯文本端到端问答留在 STEP-011。

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
