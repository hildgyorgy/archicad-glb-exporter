#!/usr/bin/env pwsh
# Windows counterpart of scripts/package-release.sh.
# Run from anywhere; paths are resolved relative to the repo root.
#
# Usage:
#   ./scripts/package-release.ps1
#   $env:AC_API_DEVKIT_DIR = "C:\API Development Kit 29.3100"   # optional override
#   $env:CODESIGN_THUMBPRINT = "<certificate thumbprint>"       # optional Authenticode signing
#   $env:ALLOW_UNSIGNED = "1"                                   # allow a local, unsigned test package

$ErrorActionPreference = "Stop"

$ProjectRoot   = (Resolve-Path "$PSScriptRoot/..").Path
$ReleaseVersion = "0.1.0-alpha"
$ArchiveName    = "DropView-GLB-Exporter-AC29-Win-x64-v$ReleaseVersion"
$AddonName      = "DropViewGLBExporter"
$BuildDir       = Join-Path $env:TEMP "dropview-glb-exporter-release\build"
$StageParent    = Join-Path $env:TEMP "dropview-glb-exporter-release\stage"
$StageDir       = Join-Path $StageParent $ArchiveName
$OutputZip      = Join-Path $ProjectRoot "dist\$ArchiveName.zip"
$DevKitDir      = if ($env:AC_API_DEVKIT_DIR) { $env:AC_API_DEVKIT_DIR } else { "$env:USERPROFILE\Downloads\API" }

if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }

cmake -S $ProjectRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DAC_API_DEVKIT_DIR="$DevKitDir" `
    -DAC_ADDON_NAME="$AddonName" `
    -DAC_ADDON_LANGUAGE="INT"
cmake --build $BuildDir --config Release

$ApxPath = Get-ChildItem -Path $BuildDir -Recurse -Filter "$AddonName.apx" | Select-Object -First 1
if (-not $ApxPath) {
    Write-Error "Release .apx not found under $BuildDir"
    exit 1
}

if ($env:CODESIGN_THUMBPRINT) {
    $cert = Get-ChildItem -Path Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $env:CODESIGN_THUMBPRINT }
    if (-not $cert) {
        Write-Error "No certificate with thumbprint $env:CODESIGN_THUMBPRINT found in the current user's store."
        exit 1
    }
    signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /sha1 $env:CODESIGN_THUMBPRINT $ApxPath.FullName
    signtool verify /pa $ApxPath.FullName
} elseif ($env:ALLOW_UNSIGNED -eq "1") {
    Write-Warning "Packaging an unsigned .apx (ALLOW_UNSIGNED=1) - local testing only."
} else {
    Write-Error "Set CODESIGN_THUMBPRINT to a code-signing certificate thumbprint, or ALLOW_UNSIGNED=1 for a local test package only."
    exit 1
}

if (Test-Path $StageParent) { Remove-Item -Recurse -Force $StageParent }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ProjectRoot "dist") -Force | Out-Null

Copy-Item $ApxPath.FullName -Destination (Join-Path $StageDir "$AddonName.apx")
Copy-Item (Join-Path $ProjectRoot "README.md") -Destination (Join-Path $StageDir "README.md")
Copy-Item (Join-Path $ProjectRoot "LICENSE") -Destination (Join-Path $StageDir "LICENSE.txt")
Copy-Item (Join-Path $ProjectRoot "THIRD_PARTY_NOTICES.txt") -Destination (Join-Path $StageDir "THIRD_PARTY_NOTICES.txt")

if (Test-Path $OutputZip) { Remove-Item $OutputZip }
Compress-Archive -Path $StageDir -DestinationPath $OutputZip

$hash = Get-FileHash -Path $OutputZip -Algorithm SHA256
"$($hash.Hash.ToLower())  $(Split-Path $OutputZip -Leaf)" | Out-File -Encoding ascii "$OutputZip.sha256"

Write-Host "Created $OutputZip"
Get-Content "$OutputZip.sha256"
