# SPDX-License-Identifier: MIT
# Build the Windows tools and prepare an image from this controller's own backup.
# All USB operations in this script are reads.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
function Run-Dotnet {
    & dotnet @args
    if ($LASTEXITCODE -ne 0) { throw "dotnet failed with exit code $LASTEXITCODE." }
}
try {
    $original = 'private/controller-original.bin'
    $image = 'build/switch22c-301b-v109.bin'
    $reader = 'host/bin/Release/net9.0/ControllerRead.dll'
    $writer = 'host-flash/bin/Release/net9.0/ControllerFlash.dll'
    $required = (Get-Content -Raw firmware/full-301b-v1.json | ConvertFrom-Json).target_sha256
    New-Item -ItemType Directory -Force private,build | Out-Null
    Run-Dotnet build host/ControllerRead.csproj -c Release --nologo
    Run-Dotnet build host-flash/ControllerFlash.csproj -c Release --nologo
    Run-Dotnet $reader identify
    if (!(Test-Path -LiteralPath $original)) { Run-Dotnet $reader backup $original }
    Run-Dotnet $writer self-test $original
    if (!(Test-Path -LiteralPath $image)) { Run-Dotnet $writer build-image $original $image }
    if ((Get-FileHash -LiteralPath $image).Hash -ne $required) {
        throw "Existing output differs from this release. Keep it separately and rerun preparation."
    }
    Run-Dotnet $writer plan $original $image
    Write-Output "Preparation finished. Backup SHA256 $((Get-FileHash -LiteralPath $original).Hash)"
    Write-Output "Image SHA256 $required"
} finally {
    Pop-Location
}
