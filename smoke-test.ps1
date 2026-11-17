# Run the qemu smoke test of the built binary from Windows.
#
#   powershell -ExecutionPolicy Bypass -File .\smoke-test.ps1
#
# Requires the FunKey SDK and qemu-user-static inside WSL (already installed).

param(
    [string]$Binary = "build/chiptune/chiptune",
    [string]$Distro = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $Distro) {
    $distros = @(wsl.exe -l -q | ForEach-Object { ($_ -replace "`0", "").Trim() } | Where-Object { $_ })
    if ($distros.Count -eq 0) { throw "WSL distribution not found." }
    $Distro = $distros[0]
}

if ($root -match '^([A-Za-z]):(.*)$') {
    $wslRoot = "/mnt/" + $matches[1].ToLower() + ($matches[2] -replace '\\', '/')
} else {
    $wslRoot = (wsl.exe -d $Distro -- wslpath -a ($root -replace '\\', '/')).Trim()
}

$bin = "$wslRoot/$($Binary -replace '\\', '/')"

wsl.exe -d $Distro -u root -- bash "$wslRoot/scripts/smoke-test.sh" $bin
if ($LASTEXITCODE -ne 0) { throw "smoke test failed (exit $LASTEXITCODE)" }
