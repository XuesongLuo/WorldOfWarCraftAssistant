# ADR-019：云端连接策略、凭据注入与用量保护

- 状态：已接受（2026-10-03）
- 日期：2026-10-03
- 负责人：项目负责人
- 决策截止点：STEP-021
- 关联：ADR-011、ADR-017、ADR-018、E-10 至 E-12、E-21、H-03
- 修订：扩展 ADR-018 的双 provider 注册表；取代 ADR-011 的未决状态

## 背景

普通用户需要在一个设置入口中选择提供方、连接方式、精确模型/部署和 API Key，同时仍要满足
截图逐次同意、关闭云端时零外发、密钥不落 SQLite/日志、失败不自动重发图片和费用上限等边界。
各厂商协议并不相同；锁定的 Codex App Server 0.159.2 也不能承载任意协议。

对随应用锁定的 `codex.exe` 使用 `app-server --strict-config --listen off` 实测：

- `wire_api="responses"`、`env_key`、`env_http_headers`、`http_headers` 和 `query_params`
  可被严格配置解析；
- `wire_api="chat"` 和 `wire_api="chat_completions"` 被拒绝，0.159.2 只接受 Responses；
- 所有自定义 provider 都关闭 App Server 请求/流自动重试，避免应用不可见的重复计费。

## 决策

### 1. 四类连接策略

| 策略 | STEP-021 状态 | Provider | 说明 |
|---|---|---|---|
| 原生 OpenAI/Responses | 已接线，Mock 验证 | `openai` | 固定 `https://api.openai.com/v1` |
| Codex 可配置兼容 Responses | 已接线，Mock 验证 | `deepseek`、`xai`、`openrouter`、`dashscope` | 只接受官方声明的 Responses/OpenResponses 端点 |
| 原生非 OpenAI 协议 | 已评估，暂不启用 | Anthropic Messages、Gemini `generateContent`、Mistral Chat | 0.159.2 无原生适配器，且 chat wire 已移除；不得伪造可用性 |
| Azure 部署式 Responses | 已接线，Mock/严格配置验证 | `azure-openai` | `https://{resource}.openai.azure.com/openai/v1`，模型字段填写部署名，API key 通过 `api-key` header，`api-version` 使用查询参数 |

OpenRouter 的 OpenResponses 端点已接线，但图片能力依赖模型及实际路由，STEP-021 默认关闭图片。
Azure 的部署名也不能证明底层模型支持图片，默认关闭图片。只有注册表能够从官方证据确定的精确
模型才允许截图；否则失败关闭。

### 2. Endpoint 与 SSRF 边界

普通设置页不接受任意 URL：OpenAI、DeepSeek、xAI、OpenRouter 使用固定域名；DashScope 由固定
区域枚举、DNS-safe Workspace ID 和官方 `*.maas.aliyuncs.com` 模板生成；Azure 由 DNS-safe
资源名和 `*.openai.azure.com/openai/v1` 模板生成。URL 中不能出现凭据。STEP-021 不开放高级
自定义 URL；未来若增加，必须另立 ADR 并加入 HTTPS、DNS/IP、重定向及私网 SSRF 防护。

### 3. 凭据与进程隔离

API Key 以 `provider/profile` 为键使用当前 Windows 用户 DPAPI 加密，密文文件与 SQLite 分离；
设置页只显示“未配置”或末尾最多四字符，密码框不会回填。C++ 只把当前选择的一把 key 作为
`WOWAI_ACTIVE_API_KEY` 写入本应用启动的 Host 子进程环境；Host 再只把该 key 传给本应用启动的
App Server，并立即从自身环境删除。不会调用 `SetEnvironmentVariable` 修改用户或机器环境，
也会剔除父环境中的其他厂商 key。

`.env.local` 只在显式 `WOWAI_DEVELOPMENT_ENV_FALLBACK=1` 时作为开发回退；产品设置与 DPAPI
优先，云端总开关关闭时不会启动 Host/App Server。

### 4. 用量、幂等、限流和审计

- 会话/UTC 日/UTC 月请求数上限和 1–100% 停止阈值在网络请求前保守计数；失败请求也计入，
  因为客户端无法证明上游未计费。
- SQLite `cloud_request_audit` 只保存 request ID、时间、provider、profile、model、目的域及是否
  含图，不保存问题、回答、图片或凭据。request ID 主键阻止重复提交。
- 已确认图片在请求进入工作线程前即从待提交状态移除；失败、取消、Host 恢复和限流均不会自动
  重发。App Server provider 的 HTTP/stream retry 均为 0。
- `429` 解析 `Retry-After` 或等价等待提示并建立最长一小时的本地冷却期；缺少等待值时使用
  30 秒。冷却只阻断并提示，不自动重试。
- 设置页连接测试固定使用 Mock，明确显示“不联网/不计费”。真实测试必须由未来单独入口再次
  授权；自动测试不得读取真实 key 或调用真实 API。

## 官方证据与限制

- [OpenAI API 概览](https://developers.openai.com/api/reference/overview) 与
  [Responses 创建接口](https://developers.openai.com/api/reference/cli/resources/responses/methods/create)
- [Codex provider 源码字段](https://github.com/openai/codex/blob/main/codex-rs/model-provider-info/src/lib.rs)
- [DeepSeek Responses](https://api-docs.deepseek.com/api/create-response/) 与
  [Vision](https://api-docs.deepseek.com/guides/vision/)
- [xAI REST/Responses](https://docs.x.ai/developers/rest-api-reference/inference) 与
  [图片理解](https://docs.x.ai/developers/model-capabilities/images/understanding)
- [OpenRouter Responses](https://openrouter.ai/docs/api/api-reference/responses/create-responses)
- [DashScope/Model Studio OpenAI 兼容接口](https://www.alibabacloud.com/help/en/model-studio/what-is-model-studio)
- [Azure OpenAI Responses](https://learn.microsoft.com/en-us/rest/api/aifoundry/azureopenai/responses)
- [Anthropic Messages](https://platform.claude.com/docs/en/api/http/messages)、
  [Gemini 原生 API](https://ai.google.dev/api)、[Mistral Chat](https://docs.mistral.ai/api)

“已接线”不等于真实账号兼容性已通过。除仓库此前记录的 DeepSeek 联调外，STEP-021 没有获得
用户对真实 API 调用和费用的再次授权，因此其他 provider 只标记为官方协议证据 + Mock/配置
验证，不能在发布门禁中宣称真实连接通过。

## 回退方案

若某兼容 Responses 服务与 0.159.2 的流式事件或结构化输出不一致，则禁用该 provider，不降级
到 chat wire、不切换模型、不改发其他域名。原生非 OpenAI 协议只有在新增受限 Host 适配器、
独立契约测试和真实人工验收后才能启用。
