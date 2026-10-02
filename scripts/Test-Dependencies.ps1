param(
    [Parameter()]
    [switch]$RequireWebViewRuntime
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -Raw (Join-Path $repositoryRoot 'eng\bootstrap-lock.json') | ConvertFrom-Json
$node = Join-Path $repositoryRoot '.tools\node\node.exe'
$npm = Join-Path $repositoryRoot '.tools\node\npm.cmd'
$vcpkg = Join-Path $repositoryRoot '.tools\vcpkg\vcpkg.exe'
$webViewRoot = Join-Path $repositoryRoot '.tools\webview2-sdk'
$webViewHeader = Join-Path $webViewRoot 'build\native\include\WebView2.h'
$webViewLoader = Join-Path $webViewRoot 'build\native\x64\WebView2LoaderStatic.lib'
$webViewVersionMarker = Join-Path $webViewRoot '.wowai-version'

foreach ($required in @($node, $npm, $vcpkg, $webViewHeader, $webViewLoader, $webViewVersionMarker, (Join-Path $repositoryRoot 'package-lock.json'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "缺少依赖验证输入：$required" }
}
if ((Get-Content -Raw -LiteralPath $webViewVersionMarker).Trim() -ne $lock.webView2Sdk.version) {
    throw 'WebView2 SDK 版本标记与 bootstrap lock 不一致。'
}

$sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include\$((Get-Content -Raw (Join-Path $repositoryRoot 'eng\toolchain.json') | ConvertFrom-Json).windowsSdk)"
$sdkFiles = @(
    (Join-Path $sdkRoot 'um\d3d11.h'),
    (Join-Path $sdkRoot 'um\dcomp.h'),
    (Join-Path $sdkRoot 'shared\dxgi1_6.h'),
    (Join-Path $sdkRoot 'um\windows.graphics.capture.interop.h'),
    (Join-Path $sdkRoot 'um\wincodec.h'),
    (Join-Path $sdkRoot 'cppwinrt\winrt\base.h')
)
foreach ($required in $sdkFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Windows 覆盖层/捕获开发文件缺失：$required"
    }
}

$webViewClientId = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
$runtimeKeys = @(
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$webViewClientId",
    "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$webViewClientId",
    "HKCU:\Software\Microsoft\EdgeUpdate\Clients\$webViewClientId"
)
$runtime = $runtimeKeys | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $runtime -and $RequireWebViewRuntime) {
    throw '未检测到 Microsoft Edge WebView2 Evergreen Runtime。'
}
$runtimeVersion = if ($runtime) { (Get-ItemProperty -LiteralPath $runtime).pv } else { 'not-installed' }

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

Write-Output "依赖基线验证通过：工具版本固定、WebView2 SDK $($lock.webView2Sdk.version)、Runtime $runtimeVersion、Windows 图形/捕获头文件完整、npm 高危漏洞为零、锁文件稳定、SBOM/NOTICE 可生成、未使用 DLL 未进入产物。"
