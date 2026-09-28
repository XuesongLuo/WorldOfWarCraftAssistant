param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CMakeArguments
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildToolsRoot = Join-Path $repositoryRoot '.tools\vs-buildtools'
if (-not (Test-Path -LiteralPath $buildToolsRoot -PathType Container)) {
    $buildToolsRoot = 'C:\WOWAI\vsbt'
}

$cmake = Join-Path $buildToolsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
    throw "找不到已登记的 CMake：$cmake"
}

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $cmake
$startInfo.UseShellExecute = $false
$startInfo.WorkingDirectory = (Get-Location).Path

# Codex Desktop 的宿主环境可能同时包含 Path 和 PATH。旧版 MSBuild 在启动
# cl.exe 时会把它们视为重复键，因此为 CMake 子进程建立单一的大写 PATH。
$pathValue = $startInfo.Environment['Path']
if ([string]::IsNullOrEmpty($pathValue)) {
    $pathValue = $startInfo.Environment['PATH']
}
[void]$startInfo.Environment.Remove('Path')
[void]$startInfo.Environment.Remove('PATH')
$startInfo.Environment.Add('PATH', $pathValue)

foreach ($argument in $CMakeArguments) {
    [void]$startInfo.ArgumentList.Add($argument)
}

$process = [System.Diagnostics.Process]::Start($startInfo)
$process.WaitForExit()
exit $process.ExitCode
