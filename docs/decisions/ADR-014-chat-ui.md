# ADR-014：聊天 UI 宿主方案

- 状态：已接受
- 日期：2026-09-28
- 负责人：Windows 客户端技术负责人
- 决策截止点：STEP-008 完成前
- 关联：C-11、G-03、G-04、G-06、G-08、G-09

## 背景

聊天 UI 需要文本输入、流式回复、滚动、字体缩放、高对比度和安全链接处理，同时必须与透明浮层、DPI 和焦点策略配合。

## 默认 PoC 假设

使用 WebView2 承载原生 HTML/CSS/TypeScript，不引入 React/Vue。C++ 只暴露严格白名单消息桥接；导航默认禁止，外部链接仅由玩家点击后交给系统浏览器。M0 先使用纯文本渲染，Markdown 与 DOMPurify 在通过 XSS 评估前不加入依赖。

## 验证与接受条件

- WebView2 Runtime 检测和缺失提示可用。
- 桥接拒绝未知消息、超长消息和非预期来源。
- 输入焦点不把按键传给 WoW；Alt+Tab、最小化和锚点丢失时浮层隐藏。
- 常见 DPI、多屏、字体缩放和高对比度下可用。

## STEP-008 PoC 结果（2026-10-01）

已完成 Win32/DirectComposition/D3D11 与 WebView2 Composition Controller 宿主、透明背景、最小聊天 UI、交互/鼠标穿透切换、严格来源与消息白名单、导航禁止和 Runtime 缺失提示。36/36 C++ 单元/组件测试通过，其中真实 Composition Controller 组件测试不加载插件、Codex 或网络。

真实 WoW 核心人工门禁 M-001 至 M-010 已通过，覆盖输入焦点隔离、Alt+Tab/最小化/退出隐藏、插件非必需路径、常见 DPI/多屏、字体缩放、高对比度和修复后的跨进程鼠标穿透。M-011 可选插件增强与 M-012 开发者隔离负向测试按计划记为 N/A，不阻塞无插件宿主决策。因此接受 WebView2 Composition Controller 作为聊天 UI 宿主方案。

## 回退方案

若 WebView2 透明合成、输入焦点或分发不可接受，改用 Direct2D/DirectWrite 的纯原生最小文本 UI；功能降级为纯文本输入输出，不放宽安全桥接。
