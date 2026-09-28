# STEP-001 验证记录

- 日期：2026-09-28
- 结论：通过
- 范围：开发环境盘点、M0 ADR 草案完整性、风险主题、失败路径、文档空白错误

## 正常路径

命令：

```powershell
pwsh -NoProfile -File .\scripts\verify-step001.ps1
```

结果：退出码 `0`。8 个必需产物存在；6 份 ADR 均含负责人、决策截止点、默认 PoC 假设和回退方案；风险清单包含 WoW 政策、浮层、Codex App Server、本地模型和资料许可主题。

## 失败路径

命令：

```powershell
pwsh -NoProfile -File .\scripts\verify-step001.ps1 -DocumentsRoot .\__missing_step001_fixture__
```

结果：退出码 `1`，明确报告缺少 ADR 文件。证明验证器不会在产物缺失时误报通过。

## 格式与仓库检查

命令：

```powershell
git diff --check
```

结果：退出码 `0`，未发现尾随空白或补丁格式错误。

## 安全与隐私检查

- 本步骤只记录工具名称、公开版本号、项目路径能力和缺失项。
- 未读取或保存 API 密钥、令牌、玩家截图、聊天正文、全局 Codex 配置、Skills、Plugins 或 MCP 连接。
- 未安装依赖，符合 STEP-001“不安装未登记依赖”的边界。
