param(
    [Parameter()]
    [string]$DocumentsRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$requiredFiles = @(
    'docs/decisions/ADR-002-overlay-positioning.md',
    'docs/decisions/ADR-003-codex-integration.md',
    'docs/decisions/ADR-004-launch-region.md',
    'docs/decisions/ADR-010-local-model-provider.md',
    'docs/decisions/ADR-014-chat-ui.md',
    'docs/decisions/ADR-015-cpp-dependencies-linkage.md',
    'docs/environment/2026-09-28-development-environment.md',
    'docs/risks/M0-risk-register.md'
)

$errors = [System.Collections.Generic.List[string]]::new()
$adrFields = @('负责人：', '决策截止点：', '## 默认 PoC 假设', '## 回退方案')

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $DocumentsRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $errors.Add("缺少文件：$relativePath")
        continue
    }

    if ($relativePath -like 'docs/decisions/ADR-*.md') {
        $content = Get-Content -Raw -Encoding UTF8 -LiteralPath $path
        foreach ($field in $adrFields) {
            if (-not $content.Contains($field)) {
                $errors.Add("$relativePath 缺少字段：$field")
            }
        }
    }
}

$riskPath = Join-Path $DocumentsRoot 'docs/risks/M0-risk-register.md'
if (Test-Path -LiteralPath $riskPath -PathType Leaf) {
    $riskContent = Get-Content -Raw -Encoding UTF8 -LiteralPath $riskPath
    foreach ($topic in @('WoW 政策', '浮层', 'Codex App Server', '本地模型', '资料')) {
        if (-not $riskContent.Contains($topic)) {
            $errors.Add("风险清单缺少主题：$topic")
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "STEP-001 文档验证通过：$($requiredFiles.Count) 个必需产物完整，6 份 ADR 均含负责人、截止点、默认假设和回退方案。"
exit 0
