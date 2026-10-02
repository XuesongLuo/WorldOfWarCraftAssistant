# STEP-008 自动验证记录（2026-10-01）

## 结论

STEP-008 代码实现、36/36 自动验证以及真实 WoW 核心人工门禁 M-001 至 M-010 全部通过。M-004 首轮缺陷已修复并复测通过；M-011、M-012 按计划分别记为可选插件未更新和开发者隔离环境 N/A。STEP-008 验证完成，ADR-014 接受。

## 已验证内容

- Win32 `WS_POPUP` 透明置顶窗口、D3D11 BGRA 设备、DirectComposition 目标/视觉树和 WebView2 Composition Controller 能在不加载 WoW 插件的组件夹具中初始化并正常销毁。无交互桌面的自动化会话只验证到控制器；页面像素呈现和 `ready` 消息必须在交互桌面验证。
- 覆盖层使用 WoW 客户区相对布局，并对小窗口、负坐标显示器和无效矩形执行安全夹取/隐藏。
- 前台策略只允许“所选 WoW 前台”或“覆盖层自身处于交互前台”；最小化、无效窗口和不可用客户区均隐藏。
- C++ ↔ WebView2 桥只接受应用拥有的 `https://wowai-overlay.invalid/index.html` 精确来源、版本 1、精确字段集合和四种白名单消息；未知来源、未知类型、未知字段、畸形 JSON、空文本及超过 16 KiB 的消息失败关闭。
- 外部链接仅允许 HTTPS 且主机精确匹配 Blizzard 白名单，避免后缀伪造；WebView 内导航、DevTools、默认上下文菜单、Host Objects、脚本对话框和内置错误页均关闭。
- 最小聊天 UI 支持输入、Enter 发送、Shift+Enter、滚动、本地模拟回复、鼠标穿透切换、ARIA live region、键盘焦点样式、高对比度和文本换行。
- 实现未连接 Codex、模型、截图/画面识别或新增插件字段。

## 命令与结果

```powershell
.\scripts\Invoke-CMake.ps1 --preset windows-msvc-debug
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify
```

- Debug 构建：通过。
- CTest：36/36 通过，包括 1 个真实 WebView2 Composition Controller 组件测试；当前自动化会话没有前台桌面，因此该用例明确报告仅完成控制器级验证，而不把页面呈现误报为成功。
- 覆盖层新增单元测试：7 个测试用例，覆盖定位、前台门控、桥接白名单、超长/畸形载荷、链接白名单和原生消息安全序列化。
- 仓库、依赖、插件静态门禁：通过。
- Prettier、ESLint、TypeScript strict、13/13 Vitest 和 esbuild：通过；esbuild 在受限沙箱内因上级目录读取被拒，使用相同锁定包装命令在沙箱外复跑通过。
- WebView2 组件测试不依赖插件、Codex、网络或真实 WoW 进程。

## 透明空白回归诊断

- 原实现用 `NavigateToString` 加载内嵌 HTML；当前 WebView2 Runtime 把其内部 `data:` 导航以 `CONNECTION_ABORTED` 结束，控制器存在但页面脚本从未发出 `ready`，表现为窗口可命中而完全透明。
- UI 改为写入应用私有的 LocalAppData 目录，并通过 WebView2 虚拟主机映射从固定 `.invalid` HTTPS 来源加载；导航和消息来源都按该完整 URI 严格校验。
- `initialized()` 现在要求控制器和受信页面 `ready` 同时成立。首次文档加载期间，宿主只在虚拟桌面外呈现 1×1 窗口，避免不可见 Composition Controller 延迟加载，同时不在屏幕上闪烁。

## 人工验证结论

已按 `docs/test-plans/STEP-008-overlay-manual-test.md` 完成核心门禁 M-001 至 M-010：焦点隔离、插件非必需模拟聊天、Alt+Tab/最小化/退出隐藏、窗口移动与跨屏、DPI/分辨率、可访问性及安全链接均通过。

M-004 首轮发现覆盖层进入“鼠标穿透”后，底层 WoW 同一区域仍无法点击。修复将顶层窗口的 `WS_EX_TRANSPARENT` 与 `WS_EX_LAYERED`、`WS_EX_NOACTIVATE` 组合，并显式返回 `HTTRANSPARENT`；组件测试验证穿透样式组合和命中测试结果，真实 WoW 于 2026-10-01 复测通过。M-011 因未更新可选插件记为 N/A；M-012 是禁止卸载系统 Runtime 的开发者隔离测试，普通人工验收记为 N/A，二者均不阻塞 STEP-008 无插件核心路径。
