$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Import-LocalEnvironment.ps1')

$node = Join-Path $repositoryRoot '.tools\node\node.exe'
$hostScript = Join-Path $repositoryRoot 'codex-host\dist\index.js'
$runtimeLock = Join-Path $repositoryRoot 'eng\codex-runtime-lock.json'
$codex = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot '.tools\codex\0.159.2') `
    -Recurse -Filter codex.exe -File | Select-Object -First 1
if (-not (Test-Path -LiteralPath $node -PathType Leaf) -or
    -not (Test-Path -LiteralPath $hostScript -PathType Leaf) -or
    -not $codex) {
    throw 'DeepSeek Host 测试缺少 Node、Host bundle 或锁定 Codex。'
}

$lock = Get-Content -Raw -LiteralPath $runtimeLock | ConvertFrom-Json
$binaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $codex.FullName).Hash.ToLowerInvariant()
if ($binaryHash -cne $lock.sha256) {
    throw 'DeepSeek Host 测试拒绝未锁定的 Codex 二进制。'
}

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $node
$startInfo.ArgumentList.Add($hostScript)
$startInfo.WorkingDirectory = $repositoryRoot
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardInput = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.CreateNoWindow = $true
$startInfo.Environment['WOWAI_CODEX_BINARY'] = $codex.FullName
$startInfo.Environment['WOWAI_CODEX_LOCK'] = $runtimeLock
$startInfo.Environment['WOWAI_CODEX_ROOT'] = Join-Path $repositoryRoot '.tools\deepseek-host-smoke'

$process = [Diagnostics.Process]::Start($startInfo)
$stderrTask = $process.StandardError.ReadToEndAsync()
$pngBase64 = ''

function Read-HostLine {
    param([int]$TimeoutSeconds)

    $readTask = $process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait([TimeSpan]::FromSeconds($TimeoutSeconds))) {
        throw '等待 Codex Host 响应超时。'
    }
    if ($null -eq $readTask.Result) {
        throw 'Codex Host 在返回响应前退出。'
    }
    return $readTask.Result | ConvertFrom-Json
}

try {
    $hello = @{
        protocolVersion = '2.0'
        messageId = [guid]::NewGuid().ToString()
        kind = 'hello'
        requestId = $null
        sequence = 0
        sentAt = [DateTime]::UtcNow.ToString('o')
        timeoutMs = $null
        payload = @{ supportedVersions = @('2.0'); maxMessageBytes = 1048576 }
    }
    $process.StandardInput.Write(($hello | ConvertTo-Json -Depth 8 -Compress) + "`n")
    $process.StandardInput.Flush()
    $ready = Read-HostLine -TimeoutSeconds 15
    if ($ready.kind -ne 'ready') {
        throw 'Codex Host 协议协商失败。'
    }

    $pngBase64 = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII='
    $pngBytes = [Convert]::FromBase64String($pngBase64)
    $sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($pngBytes)).ToLowerInvariant()
    $requestId = [guid]::NewGuid().ToString()
    $request = @{
        schemaVersion = '2.0'
        requestId = $requestId
        conversationId = [guid]::NewGuid().ToString()
        createdAt = [DateTime]::UtcNow.ToString('o')
        mode = 'general'
        locale = 'zh-CN'
        gameFlavor = 'retail'
        question = 'Return a concise structured answer confirming whether the image input decoded.'
        character = @{ region = 'cn'; realm = $null; name = $null; classId = $null; specializationId = $null; level = $null }
        images = @(@{
            id = [guid]::NewGuid().ToString()
            mimeType = 'image/png'
            captureScope = 'selected-region'
            sha256 = $sha256
            dataBase64 = $pngBase64
            privacyMaskApplied = $false
            userConfirmed = $true
            uploadDestination = 'deepseek'
            uploadPurpose = 'visual-question'
            uploadConfirmedAt = [DateTime]::UtcNow.ToString('o')
            consentNoticeVersion = 1
        })
        observations = @()
        privacy = @{ selectedWindowOnly = $true; screenObservationEnabled = $false; rawFramesPersisted = $false }
        client = @{ addonVersion = $null; companionVersion = '0.1.0-dev'; uiScale = $null }
        runtime = @{ engine = 'codex'; provider = 'deepseek'; model = 'deepseek-flash'; allowCloudUpload = $true }
    }
    $envelope = @{
        protocolVersion = '2.0'
        messageId = [guid]::NewGuid().ToString()
        kind = 'request'
        requestId = $requestId
        sequence = 0
        sentAt = [DateTime]::UtcNow.ToString('o')
        timeoutMs = 120000
        payload = $request
    }
    $process.StandardInput.Write(($envelope | ConvertTo-Json -Depth 12 -Compress) + "`n")
    $process.StandardInput.Flush()
    $result = Read-HostLine -TimeoutSeconds 125
    if ($result.kind -ne 'response' -or $result.payload.status -ne 'completed') {
        $errorCode = if ($result.payload.code) { $result.payload.code } else { 'UNKNOWN' }
        $errorMessage = if ($result.payload.message) { $result.payload.message } else { '没有错误详情' }
        throw "DeepSeek Host 请求失败：$errorCode；$errorMessage"
    }
    if ($result.payload.usage.provider -ne 'deepseek' -or $result.payload.usage.imageUsed -ne $true) {
        throw 'DeepSeek Host 返回的 provider 或图片使用标记不正确。'
    }

    [pscustomobject]@{
        Test = 'host-codex-deepseek-vision'
        Status = $result.payload.status
        Provider = $result.payload.usage.provider
        ImageUsed = $result.payload.usage.imageUsed
    } | Format-Table -AutoSize
}
finally {
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(5000)) {
        $process.Kill($true)
    }
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0 -and -not [string]::IsNullOrWhiteSpace($stderr)) {
        $safeDiagnostic = $stderr.Replace($env:DEEPSEEK_API_KEY, '[REDACTED]')
        if ($pngBase64.Length -gt 0) {
            $safeDiagnostic = $safeDiagnostic.Replace($pngBase64, '[IMAGE_REDACTED]')
        }
        if ($safeDiagnostic.Length -gt 2000) {
            $safeDiagnostic = $safeDiagnostic.Substring($safeDiagnostic.Length - 2000)
        }
        Write-Warning "Codex Host 诊断（已脱敏）：$safeDiagnostic"
    }
}
