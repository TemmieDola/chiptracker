# Build an .opk for the RG Nano from Windows, inside WSL.
#
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -App hello -Distro Ubuntu-24.04

param(
    [string]$App = "chiptune",
    [string]$Distro = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $Distro) {
    $distros = @(wsl.exe -l -q | ForEach-Object { ($_ -replace "`0", "").Trim() } | Where-Object { $_ })
    if ($distros.Count -eq 0) {
        throw "WSL distribution not found. Install one:  wsl --install -d Ubuntu-24.04"
    }
    $Distro = $distros[0]
}

Write-Host "== WSL distro: $Distro"
Write-Host "== project:    $root"

$wslRoot = $null
if ($root -match '^([A-Za-z]):(.*)$') {
    $wslRoot = "/mnt/" + $matches[1].ToLower() + ($matches[2] -replace '\\', '/')
} else {
    $forward = $root -replace '\\', '/'
    $wslRoot = (wsl.exe -d $Distro -- wslpath -a $forward).Trim()
}
if (-not $wslRoot) {
    throw "Cannot translate path $root inside WSL"
}

wsl.exe -d $Distro -- env "APP=$App" bash "$wslRoot/scripts/build.sh"
if ($LASTEXITCODE -ne 0) {
    throw "build failed (exit $LASTEXITCODE)"
}

Write-Host ""
Write-Host "== result: $root\dist\$App.opk"
