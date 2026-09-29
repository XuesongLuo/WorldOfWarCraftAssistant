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

$nodeRoot = Join-Path $repositoryRoot '.tools\node'
$vcpkgRoot = Join-Path $repositoryRoot '.tools\vcpkg'
$sevenZipRoot = Join-Path $repositoryRoot '.tools\bin\7zip'
$cmakeRoot = Split-Path -Parent $cmake
$ninjaRoot = Join-Path $buildToolsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'

# Codex Desktop 的宿主环境可能同时包含 Path 和 PATH。旧版 MSBuild 在启动
# cl.exe 时会把它们视为重复键，因此为 CMake 子进程建立单一的大写 PATH。
$pathValue = $startInfo.Environment['Path']
if ([string]::IsNullOrEmpty($pathValue)) {
    $pathValue = $startInfo.Environment['PATH']
}
[void]$startInfo.Environment.Remove('Path')
[void]$startInfo.Environment.Remove('PATH')
$toolPaths = @($cmakeRoot, $ninjaRoot, $sevenZipRoot, $nodeRoot, $vcpkgRoot) | Where-Object {
    Test-Path -LiteralPath $_ -PathType Container
}
$startInfo.Environment.Add('PATH', (($toolPaths + $pathValue.Split(';')) -join ';'))
$startInfo.Environment['VCPKG_ROOT'] = $vcpkgRoot
$startInfo.Environment['VCPKG_FORCE_SYSTEM_BINARIES'] = '1'

foreach ($argument in $CMakeArguments) {
    [void]$startInfo.ArgumentList.Add($argument)
}

$process = [System.Diagnostics.Process]::Start($startInfo)
$process.WaitForExit()
exit $process.ExitCode
