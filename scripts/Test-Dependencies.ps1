$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -Raw (Join-Path $repositoryRoot 'eng\bootstrap-lock.json') | ConvertFrom-Json
$node = Join-Path $repositoryRoot '.tools\node\node.exe'
$npm = Join-Path $repositoryRoot '.tools\node\npm.cmd'
$vcpkg = Join-Path $repositoryRoot '.tools\vcpkg\vcpkg.exe'

foreach ($required in @($node, $npm, $vcpkg, (Join-Path $repositoryRoot 'package-lock.json'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "缺少依赖验证输入：$required" }
}

$nodeVersion = (& $node --version).TrimStart('v')
$npmVersion = (& $npm --version).Trim()
if ($nodeVersion -ne $lock.node.version -or $npmVersion -ne $lock.npm) {
    throw "Node/npm 版本漂移：Node $nodeVersion，npm $npmVersion"
}

$nodeRoot = Split-Path -Parent $node
$previousPath = $env:PATH
try {
    $env:PATH = "$nodeRoot;$previousPath"
    & $npm audit --audit-level=high
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    $env:PATH = $previousPath
}

git -C $repositoryRoot diff --exit-code -- package-lock.json
if ($LASTEXITCODE -ne 0) { throw 'package-lock.json 被依赖验证修改。' }

& (Join-Path $PSScriptRoot 'Generate-DependencyArtifacts.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$forbiddenRuntimeDlls = @('sqlite3.dll', 'spdlog.dll', 'fmt.dll')
$buildOutput = Join-Path $repositoryRoot 'out\build\windows-msvc-debug\companion\Debug'
if (Test-Path -LiteralPath $buildOutput -PathType Container) {
    $unexpected = Get-ChildItem -LiteralPath $buildOutput -File | Where-Object Name -In $forbiddenRuntimeDlls
    if ($unexpected) { throw "未使用的第三方运行库进入构建产物：$($unexpected.Name -join ', ')" }
}

Write-Output '依赖基线验证通过：工具版本固定、npm 高危漏洞为零、锁文件稳定、SBOM/NOTICE 可生成、未使用 DLL 未进入产物。'
