$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildToolsRoot = Join-Path $repositoryRoot '.tools\vs-buildtools'
if (-not (Test-Path -LiteralPath $buildToolsRoot -PathType Container)) {
    $buildToolsRoot = 'C:\WOWAI\vsbt'
}
$cmakeBin = Join-Path $buildToolsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $buildToolsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'

if (-not (Test-Path -LiteralPath $cmakeBin -PathType Container)) {
    throw "找不到已登记的 CMake：$cmakeBin。请先按 docs/environment 中的记录安装 Build Tools。"
}

$pathEntries = @($cmakeBin)
if (Test-Path -LiteralPath $ninjaBin -PathType Container) {
    $pathEntries += $ninjaBin
}

$env:PATH = ($pathEntries + $env:PATH.Split(';')) -join ';'
Write-Output "已为当前 PowerShell 会话启用登记的 CMake/Ninja：$buildToolsRoot"
