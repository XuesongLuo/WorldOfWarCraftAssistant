# STEP-001 开发环境检查记录

- 检查日期：2026-09-28
- 检查范围：仓库、Windows 开发机、M0 所需工具、CI 入口
- 执行者：Codex 开发会话
- 结论：基础命令行环境可用，但 C++ 工具链和项目包管理工具尚未就绪；CI 尚未配置

## 仓库状态

- Git 仓库已初始化，当前分支为 `master`，尚无提交。
- 初始输入只有 PRD、技术设计和开发进度三份 Markdown，均为未跟踪文件。
- 未检测到 Git remote，因此无法确认远端 CI 执行权限。
- 未检测到 `.github/workflows`、Azure Pipelines 或 GitLab CI 配置。
- 工作区路径包含空格，可作为路径处理边界样本；当前用户名和路径未包含非 ASCII 字符，后续 CI 需补充该样本。

## 已检测到

| 组件 | 检测结果 | 备注 |
|---|---|---|
| Windows | 可用 | 注册表产品字符串为 Windows 10 Pro 25H2，内核 build 为 26200.9457；该 build 属于 Windows 11 代际，但系统名称探测结果不一致，STEP-002 仍以锁定 Windows 11 SDK 的编译验证为准 |
| Git | 2.53.0.windows.3 | 来自 Codex 自带运行时，可用于仓库操作 |
| Node.js | v24.19.0 | 来自 Codex 自带运行时；不是项目锁定运行时，不能作为发布依赖 |
| Codex CLI | 0.158.0-alpha.2.1 | 来自 Codex Desktop；仅作为可用性信号，STEP-009 必须改为项目锁定来源和哈希 |

## 未检测到或尚未确认

| 组件 | 状态 | 处理步骤 |
|---|---|---|
| Visual Studio 2022 Build Tools / MSVC v143 | 已安装 | Build Tools 17.14.37710.0；MSVC Tools 14.44.35207；编译器 19.44.35229；专用短路径 `C:\WOWAI\vsbt` |
| Windows 11 SDK | 已安装 | 10.0.26100.0，已由真实 CMake 配置选中 |
| CMake | 已安装 | 3.31.6-msvc6，随 Build Tools 组件安装 |
| Ninja | 已安装 | 1.12.1；当前 preset 保留 Visual Studio 生成器作为可靠基线 |
| vcpkg | VS 组件可用但未作为项目依赖 | STEP-003 仍须以项目固定 baseline/commit 引入，不使用 VS 隐式状态 |
| npm / npx | 未检测到 | STEP-003 配置项目锁定 Node.js/npm；当前 Node.js 不完整 |
| WebView2 Runtime | 注册表与已安装程序中未检测到 | STEP-008 验证 Evergreen Runtime 检测和安装策略 |
| Ollama / LM Studio | 未检测到 | ADR-010 决策后，在 STEP-011 安装所选提供方 |
| GitHub CLI / Git remote / CI | 未检测到 | STEP-002 建立 CI 文件；配置远端后再验证实际执行 |
| WoW 正式服客户端 | 未在本步骤主动扫描磁盘 | STEP-005 人工测试前由项目负责人提供或确认 |

## 本步骤不执行安装的原因

STEP-001 明确要求“不安装未登记依赖”。本记录先把缺失项登记到对应 ADR 和后续步骤；只有选定精确版本、来源和用途后才安装。能放入仓库工具目录的依赖优先项目本地化；MSVC、Windows SDK 和 WebView2 Runtime 等系统组件按机器级依赖管理，不伪装成项目包。

进入 STEP-002 后，已验证微软签名的 Visual Studio Build Tools 17.14.37710.0 引导程序（SHA-256 `985969F472CAAD75D993A5CB4C35A6A4271460CC12B343E2433B994D173AA990`）。安装器不接受当前项目深层路径，因此工具链使用专用短路径 `C:\WOWAI\vsbt`；项目脚本不会搜索其他全局位置。

## 复查命令摘要

```powershell
git status --short --branch
Get-Command git,cmake,ninja,node,npm,npx,codex,vcpkg,cl,msbuild,gh,ollama
git --version
node --version
codex --version
git remote -v
```

检查过程中未读取用户全局 Codex 配置、Skills、Plugins、MCP 连接、密钥或私人游戏数据。
