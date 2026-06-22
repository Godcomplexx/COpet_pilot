$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$assetsDir = Join-Path $projectRoot "app\src\main\assets"
$targetDir = Join-Path $assetsDir "model-ru"
$workDir = Join-Path $projectRoot ".download"
$zipPath = Join-Path $workDir "vosk-model-small-ru-0.22.zip"
$extractDir = Join-Path $workDir "extract"
$url = "https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip"
$minZipBytes = 40000000

New-Item -ItemType Directory -Force -Path $workDir | Out-Null
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

if ((Test-Path $zipPath) -and ((Get-Item $zipPath).Length -lt $minZipBytes)) {
    Write-Host "Removing incomplete archive: $zipPath"
    Remove-Item -Force $zipPath
}

if (!(Test-Path $zipPath)) {
    Write-Host "Downloading $url"
    curl.exe -L $url -o $zipPath
}

if ((Get-Item $zipPath).Length -lt $minZipBytes) {
    throw "Downloaded archive is too small. Check internet connection and run this script again."
}

if (Test-Path $extractDir) {
    Remove-Item -Recurse -Force $extractDir
}
Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

$modelRoot = Get-ChildItem -Path $extractDir -Directory |
    Where-Object { $_.Name -like "vosk-model-small-ru-*" } |
    Select-Object -First 1

if ($null -eq $modelRoot) {
    throw "Model directory not found in archive."
}

if (Test-Path $targetDir) {
    Remove-Item -Recurse -Force $targetDir
}
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

Copy-Item -Path (Join-Path $modelRoot.FullName "*") -Destination $targetDir -Recurse -Force

Write-Host "Done: $targetDir"
Write-Host "Now build/run the app from Android Studio."
