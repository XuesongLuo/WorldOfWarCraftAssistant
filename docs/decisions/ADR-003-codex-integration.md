# ADR-003：Codex 接入路线

- 状态：草案
- 日期：2026-09-28
- 负责人：AI 运行时技术负责人
- 决策截止点：STEP-010 完成前
- 关联：E-02、E-03、E-05 至 E-08、E-22

## 背景

产品需要在自定义客户端中管理认证、会话、流式事件、取消和审批。C++ 主进程不能直接依赖实验性事件结构，Codex 也不得获得 Shell、文件写入、计算机控制或未知工具能力。

## 默认 PoC 假设

TypeScript Host 启动锁定版本的 `codex app-server`，使用默认 JSONL/stdio 传输。Host 先完成 `initialize`/`initialized`，再管理 thread 与 turn，并把事件归一化为项目自有协议。所有命令执行、文件修改、权限、网络和未知副作用审批都由 Host 明确拒绝。

官方文档依据（核对日期 2026-09-28）：

- App Server 面向需要认证、对话历史、审批和流式事件的深度产品集成。
- 默认 stdio 传输是换行分隔 JSON；WebSocket 仍标记为实验性且不支持生产负载。
- CLI 可为当前版本生成 TypeScript 与 JSON Schema，生成物必须和锁定的 Codex 版本一起保存。
- Codex SDK 更适合自动化任务或 CI；若 App Server 无法满足成熟度要求，则作为备选路线重新评估。

参考：<https://learn.chatgpt.com/docs/app-server>、<https://learn.chatgpt.com/docs/codex-sdk>

## 验证与接受条件

- 锁定 Codex 版本、来源、哈希、Schema 和许可证。
- initialize、thread、turn、流式事件、取消、超时、崩溃和版本错误测试通过。
- 未知事件安全忽略或失败关闭；所有副作用请求均被拒绝且有测试证据。
- 使用应用独立配置、状态和空工作目录，不读取用户全局 Codex 配置。

## 回退方案

若 App Server 版本在 M0 内无法稳定锁定或审批不能失败关闭，暂停真实 Codex 接入，继续使用确定性 `ICodexRuntime` 模拟器；随后评估锁定版 TypeScript Codex SDK Sidecar。不得通过放宽审批或开放工具来绕过阻断。
