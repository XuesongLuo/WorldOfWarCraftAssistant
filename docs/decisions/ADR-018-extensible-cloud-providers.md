# ADR-018：受控、可扩展的云端模型提供方注册表

- 状态：已接受（2026-10-02）
- 日期：2026-10-02
- 负责人：项目负责人
- 决策截止点：STEP-012 云端联调
- 关联：ADR-017、E-12、E-14、E-21、D-08

## 背景

产品不应要求普通玩家运行本地大模型，也不应把业务协议绑定到 OpenAI。当前需要先使用 DeepSeek
API 联调，并为以后增加其他 OpenAI-compatible 云端模型保留稳定扩展点。

## 决策

1. C++、请求契约和 UI 使用稳定的 provider ID 与 model，不依赖具体 API URL。
2. TypeScript Host 维护受控 provider 注册表。注册项包含显示名、固定 HTTPS base URL、Codex
   provider ID、凭据环境变量、Responses 协议和明确的视觉能力规则。
3. 首个联调目标为 `deepseek` / `deepseek-flash`，凭据只从 `DEEPSEEK_API_KEY` 读取。依据 DeepSeek
   官方文档，该模型支持 Responses API、JSON Schema 结构化输出和图片输入。
4. OpenAI 保留为注册表中的另一个提供方，读取 `OPENAI_API_KEY`。请求不得在正文中携带密钥。
5. 图片的 `uploadDestination` 必须与请求 provider 完全相同；没有注册视觉能力的模型在创建
   App Server turn 前返回 `MODEL_CAPABILITY_MISSING`。
6. 不开放用户任意填写 base URL 或 env key，避免 SSRF、密钥错发和不透明的数据目的地。新增提供方
   必须提交注册项、官方能力依据、契约测试、失败关闭测试和独立人工验收记录。

## 验证条件

- DeepSeek App Server 参数固定为官方 base URL、`DEEPSEEK_API_KEY` 和 `wire_api="responses"`。
- 缺少凭据、同意、精确模型或 provider 不匹配时，在线程创建前失败关闭。
- `deepseek-flash` 可走文字与逐张确认截图路径；未登记视觉能力的模型拒绝截图。
- 切换提供方不会改变截图预处理、临时文件清理、工具拒绝和持续观察本地隔离边界。

## 依据

- [DeepSeek Responses API](https://api-docs.deepseek.com/guides/responses_api/)
- [DeepSeek Vision](https://api-docs.deepseek.com/guides/vision/)
- [OpenAI Codex app-server provider configuration](https://developers.openai.com/siwc/token-sharing-open-source/codex-app-server)
