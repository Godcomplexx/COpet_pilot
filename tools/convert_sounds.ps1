$ErrorActionPreference = "Stop"

$root = (Get-Item (Split-Path -Parent $PSScriptRoot)).FullName
$sounds = Join-Path $root "sounds"
$output = Join-Path $root "main\assets\sounds"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw "ffmpeg is required to convert product sound assets"
}

New-Item -ItemType Directory -Force $output | Out-Null

function Convert-Clip {
    param(
        [string]$InputName,
        [string]$OutputName,
        [string]$Start,
        [string]$End,
        [string]$FadeFilter
    )

    $inputPath = Join-Path $sounds $InputName
    $outputPath = Join-Path $output $OutputName
    if (-not (Test-Path -LiteralPath $inputPath)) {
        throw "Missing source sound: $inputPath"
    }

    & ffmpeg -y -hide_banner -loglevel error `
        -ss $Start -to $End -i $inputPath `
        -af $FadeFilter -ac 1 -ar 16000 `
        -c:a pcm_s16le -f s16le $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed for $InputName"
    }
}

Convert-Clip `
    "matthewvakaliuk73627-mouse-click-290204.mp3" `
    "menu_confirm.pcm" "0.04" "0.21" `
    "afade=t=in:st=0:d=0.005,afade=t=out:st=0.15:d=0.02"
Convert-Clip `
    "universfield-new-notification-040-493469.mp3" `
    "menu_move.pcm" "0.12" "0.33" `
    "afade=t=in:st=0:d=0.005,afade=t=out:st=0.18:d=0.03"
Convert-Clip `
    "universfield-notification-beep-229154.mp3" `
    "focus_start.pcm" "0.20" "1.60" `
    "afade=t=in:st=0:d=0.01,afade=t=out:st=1.35:d=0.05"
Convert-Clip `
    "universfield-positive-notification-351299.mp3" `
    "focus_complete.pcm" "0.07" "0.66" `
    "afade=t=in:st=0:d=0.01,afade=t=out:st=0.54:d=0.05"

Get-Item (Join-Path $output "*.pcm") |
    Select-Object Name, Length,
        @{Name="DurationSeconds"; Expression={
            [math]::Round($_.Length / 2 / 16000, 3)
        }}
