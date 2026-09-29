$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildToolsRoot = Join-Path $repositoryRoot '.tools\vs-buildtools'
if (-not (Test-Path -LiteralPath $buildToolsRoot -PathType Container)) {
    $buildToolsRoot = 'C:\WOWAI\vsbt'
}
$cmakeBin = Join-Path $buildToolsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $buildToolsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$nodeBin = Join-Path $repositoryRoot '.tools\node'
$vcpkgBin = Join-Path $repositoryRoot '.tools\vcpkg'
$projectBin = Join-Path $repositoryRoot '.tools\bin'
$sevenZipBin = Join-Path $projectBin '7zip'

if (-not (Test-Path -LiteralPath $cmakeBin -PathType Container)) {
    throw "找不到已登记的 CMake：$cmakeBin。请先按 docs/environment 中的记录安装 Build Tools。"
}

$pathEntries = @($cmakeBin)
if (Test-Path -LiteralPath $ninjaBin -PathType Container) {
    $pathEntries += $ninjaBin
}
if (Test-Path -LiteralPath $nodeBin -PathType Container) {
    $pathEntries += $nodeBin
}
if (Test-Path -LiteralPath $vcpkgBin -PathType Container) {
    $pathEntries += $vcpkgBin
}
if (Test-Path -LiteralPath $sevenZipBin -PathType Container) {
    $pathEntries += $sevenZipBin
}

$env:PATH = ($pathEntries + $env:PATH.Split(';')) -join ';'
$env:VCPKG_ROOT = $vcpkgBin
$env:VCPKG_FORCE_SYSTEM_BINARIES = '1'
Write-Output "已为当前 PowerShell 会话启用登记的 CMake/Ninja、Node/npm 和 vcpkg。"
