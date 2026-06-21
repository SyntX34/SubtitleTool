# download_model.ps1 — fetch a whisper.cpp GGML model (Windows).
#
# Usage:
#   .\scripts\download_model.ps1                # downloads ggml-base.en.bin
#   .\scripts\download_model.ps1 -Model small.en
#   .\scripts\download_model.ps1 -Model medium

param(
    [string]$Model = "base.en"
)

$ErrorActionPreference = "Stop"

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ModelsDir  = Join-Path $ScriptDir "..\models"
$BaseUrl    = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main"

New-Item -ItemType Directory -Force -Path $ModelsDir | Out-Null

$FileName = "ggml-$Model.bin"
$Dest     = Join-Path $ModelsDir $FileName
$Url      = "$BaseUrl/$FileName"

if (Test-Path $Dest) {
    Write-Host "Model already exists: $Dest"
    exit 0
}

Write-Host "Downloading $FileName ..."
Write-Host "  from: $Url"
Write-Host "  to  : $Dest"
Write-Host ""

Invoke-WebRequest -Uri $Url -OutFile "$Dest.part"
Move-Item "$Dest.part" $Dest

Write-Host ""
Write-Host "Done. Saved to $Dest"
Write-Host ""
Write-Host "Available model names: tiny.en tiny base.en base small.en small ``"
Write-Host "                        medium.en medium large-v3"
