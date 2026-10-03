$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$environmentFile = Join-Path $repositoryRoot '.env.local'
$allowedNames = @(
    'WOWAI_MODEL_PROVIDER',
    'WOWAI_CLOUD_MODEL',
    'WOWAI_CLOUD_UPLOAD_CONSENT',
    'WOWAI_DEVELOPMENT_ENV_FALLBACK',
    'WOWAI_CLOUD_ORGANIZATION',
    'WOWAI_DASHSCOPE_REGION',
    'WOWAI_DASHSCOPE_WORKSPACE',
    'WOWAI_AZURE_RESOURCE',
    'WOWAI_AZURE_API_VERSION',
    'DEEPSEEK_API_KEY',
    'OPENAI_API_KEY',
    'XAI_API_KEY',
    'OPENROUTER_API_KEY',
    'DASHSCOPE_API_KEY',
    'AZURE_OPENAI_API_KEY'
)

if (-not (Test-Path -LiteralPath $environmentFile -PathType Leaf)) {
    throw "找不到本机环境文件：$environmentFile"
}

$loadedNames = [System.Collections.Generic.List[string]]::new()
foreach ($line in Get-Content -LiteralPath $environmentFile -Encoding UTF8) {
    $trimmed = $line.Trim()
    if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) {
        continue
    }
    if ($trimmed -notmatch '^([A-Z][A-Z0-9_]*)=(.*)$') {
        throw '本机环境文件包含无效行；只允许 NAME=value。'
    }
    $name = $Matches[1]
    $value = $Matches[2].Trim()
    if ($name -notin $allowedNames) {
        throw "本机环境文件包含未允许的变量：$name"
    }
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "本机环境变量不能为空：$name"
    }
    [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    $loadedNames.Add($name)
}

if ($env:WOWAI_MODEL_PROVIDER -eq 'deepseek' -and
    ($env:DEEPSEEK_API_KEY -eq 'replace-with-your-deepseek-api-key')) {
    [Environment]::SetEnvironmentVariable('DEEPSEEK_API_KEY', $null, 'Process')
    throw '请先在 .env.local 中填写真实 DEEPSEEK_API_KEY。'
}

[Environment]::SetEnvironmentVariable('WOWAI_DEVELOPMENT_ENV_FALLBACK', '1', 'Process')

Write-Output ("已将本机云端配置载入当前 PowerShell 进程：{0}。密钥值未显示。" -f
    (($loadedNames | Sort-Object -Unique) -join ', '))
