param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$NpmArguments
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$nodeRoot = Join-Path $repositoryRoot '.tools\node'
$npm = Join-Path $nodeRoot 'npm.cmd'
if (-not (Test-Path -LiteralPath $npm -PathType Leaf)) {
    throw "找不到项目 Node/npm：$npm。请先运行 scripts/Bootstrap-Dependencies.ps1。"
}

$previousPath = $env:PATH
try {
    $env:PATH = "$nodeRoot;$previousPath"
    & $npm @NpmArguments
    exit $LASTEXITCODE
}
finally {
    $env:PATH = $previousPath
}
