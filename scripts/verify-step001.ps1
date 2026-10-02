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
    'docs/decisions/ADR-016-overlay-primary-addon-optional.md',
    'docs/environment/2026-09-28-development-environment.md',
    'docs/environment/2026-10-01-overlay-vision-environment.md',
    'docs/risks/M0-risk-register.md'
)

$errors = [System.Collections.Generic.List[string]]::new()
$commonAdrFields = @('负责人：', '## 回退方案')

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $DocumentsRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $errors.Add("缺少文件：$relativePath")
        continue
    }

    if ($relativePath -like 'docs/decisions/ADR-*.md') {
        $content = Get-Content -Raw -Encoding UTF8 -LiteralPath $path
        foreach ($field in $commonAdrFields) {
            if (-not $content.Contains($field)) {
                $errors.Add("$relativePath 缺少字段：$field")
            }
        }
        if ($content.Contains('## 默认 PoC 假设')) {
            foreach ($field in @('决策截止点：', '## 默认 PoC 假设')) {
                if (-not $content.Contains($field)) {
                    $errors.Add("$relativePath 缺少草案字段：$field")
                }
            }
        } elseif (-not $content.Contains('## 决策')) {
            $errors.Add("$relativePath 缺少已接受 ADR 的决策章节。")
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

Write-Output "环境与 ADR 文档验证通过：$($requiredFiles.Count) 个必需产物完整，草案与已接受 ADR 字段符合各自状态。"
exit 0
