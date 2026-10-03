$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'Import-LocalEnvironment.ps1')

if ($env:WOWAI_MODEL_PROVIDER -ne 'deepseek') {
    throw '真实 DeepSeek 测试要求 WOWAI_MODEL_PROVIDER=deepseek。'
}
if ($env:WOWAI_CLOUD_MODEL -ne 'deepseek-flash') {
    throw '图片测试要求 WOWAI_CLOUD_MODEL=deepseek-flash。'
}
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) {
    throw 'DEEPSEEK_API_KEY 未加载。'
}

$headers = @{
    Authorization = "Bearer $env:DEEPSEEK_API_KEY"
    'Content-Type' = 'application/json'
}
$booleanSchema = @{
    type = 'object'
    additionalProperties = $false
    required = @('ok')
    properties = @{
        ok = @{ type = 'boolean' }
    }
}

function Invoke-DeepSeekResponse {
    param(
        [Parameter(Mandatory = $true)]
        [object]$InputValue,

        [Parameter(Mandatory = $true)]
        [string]$SchemaName
    )

    $body = @{
        model = $env:WOWAI_CLOUD_MODEL
        input = $InputValue
        max_output_tokens = 128
        text = @{
            format = @{
                type = 'json_schema'
                name = $SchemaName
                schema = $booleanSchema
            }
        }
    } | ConvertTo-Json -Depth 12 -Compress

    return Invoke-RestMethod -Method Post -Uri 'https://api.deepseek.com/responses' `
        -Headers $headers -Body $body -TimeoutSec 60
}

function Get-ValidatedOutput {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Response
    )

    $textPart = $Response.output |
        Where-Object { $_.type -eq 'message' } |
        ForEach-Object { $_.content } |
        Where-Object { $_.type -eq 'output_text' } |
        Select-Object -First 1
    if ($null -eq $textPart -or [string]::IsNullOrWhiteSpace($textPart.text)) {
        throw 'DeepSeek 响应缺少 output_text。'
    }
    $parsed = $textPart.text | ConvertFrom-Json
    if ($parsed.ok -ne $true) {
        throw 'DeepSeek 结构化响应未确认测试成功。'
    }
}

$textResponse = Invoke-DeepSeekResponse -SchemaName 'wowai_text_smoke' `
    -InputValue 'Return JSON with ok=true. Do not include any other fields.'
Get-ValidatedOutput -Response $textResponse

$onePixelPng = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII='
$visionInput = @(
    @{
        role = 'user'
        content = @(
            @{ type = 'input_text'; text = 'Return JSON with ok=true if the image input was decoded.' }
            @{ type = 'input_image'; image_url = "data:image/png;base64,$onePixelPng"; detail = 'low' }
        )
    }
)
$visionResponse = Invoke-DeepSeekResponse -SchemaName 'wowai_vision_smoke' `
    -InputValue $visionInput
Get-ValidatedOutput -Response $visionResponse

@(
    [pscustomobject]@{
        Test = 'text-structured-output'
        Status = $textResponse.status
        Model = $textResponse.model
        TotalTokens = $textResponse.usage.total_tokens
    }
    [pscustomobject]@{
        Test = 'vision-inline-png'
        Status = $visionResponse.status
        Model = $visionResponse.model
        TotalTokens = $visionResponse.usage.total_tokens
    }
) | Format-Table -AutoSize
