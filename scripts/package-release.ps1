#!/usr/bin/env pwsh
# Builds and packages the Archicad 28 and 29 Windows add-ons together.
# Set AC28_API_DEVKIT_DIR and AC29_API_DEVKIT_DIR to the matching DevKit roots.
# Set CODESIGN_THUMBPRINT for Authenticode signing or ALLOW_UNSIGNED=1 for testing.

$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path "$PSScriptRoot/..").Path
$ReleaseVersion = if ($env:RELEASE_VERSION) { $env:RELEASE_VERSION } else { "0.3.0-alpha" }
$ArchiveName = "DropView-GLB-Exporter-AC28-29-Windows-x64-v$ReleaseVersion"
$AddonName = "DropViewGLBExporter"
$TemporaryRoot = Join-Path $env:TEMP "dropview-glb-exporter-release"
$StageDir = Join-Path $TemporaryRoot "stage\$ArchiveName"
$OutputZip = Join-Path $ProjectRoot "dist\$ArchiveName.zip"

$Targets = @(
    @{ Version = 28; DevKit = $env:AC28_API_DEVKIT_DIR; Toolset = "v142" },
    @{ Version = 29; DevKit = $env:AC29_API_DEVKIT_DIR; Toolset = "v143" }
)

if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ProjectRoot "dist") -Force | Out-Null

foreach ($target in $Targets) {
    if (-not $target.DevKit) {
        throw "Set AC$($target.Version)_API_DEVKIT_DIR to the Archicad $($target.Version) API DevKit root."
    }

    $BuildDir = Join-Path $TemporaryRoot "build-ac$($target.Version)"
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }

    cmake -S $ProjectRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 -T $target.Toolset `
        -DAC_API_DEVKIT_DIR="$($target.DevKit)" `
        -DAC_ADDON_NAME="$AddonName" `
        -DAC_ADDON_LANGUAGE="INT" `
        -DDROPVIEW_VERSION="$ReleaseVersion"
    cmake --build $BuildDir --config Release

    $ApxPath = Get-ChildItem -Path $BuildDir -Recurse -Filter "$AddonName.apx" | Select-Object -First 1
    if (-not $ApxPath) { throw "Release .apx not found under $BuildDir" }

    if ($env:CODESIGN_THUMBPRINT) {
        $cert = Get-ChildItem -Path Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $env:CODESIGN_THUMBPRINT }
        if (-not $cert) { throw "No certificate with thumbprint $env:CODESIGN_THUMBPRINT found." }
        signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /sha1 $env:CODESIGN_THUMBPRINT $ApxPath.FullName
        signtool verify /pa $ApxPath.FullName
    } elseif ($env:ALLOW_UNSIGNED -ne "1") {
        throw "Set CODESIGN_THUMBPRINT, or ALLOW_UNSIGNED=1 for an unsigned test package."
    }

    $VersionDir = Join-Path $StageDir "Archicad $($target.Version)"
    New-Item -ItemType Directory -Path $VersionDir -Force | Out-Null
    Copy-Item $ApxPath.FullName -Destination (Join-Path $VersionDir "$AddonName.apx")
}

Copy-Item (Join-Path $ProjectRoot "README.md") -Destination (Join-Path $StageDir "README.md")
Copy-Item (Join-Path $ProjectRoot "LICENSE") -Destination (Join-Path $StageDir "LICENSE.txt")
Copy-Item (Join-Path $ProjectRoot "THIRD_PARTY_NOTICES.txt") -Destination (Join-Path $StageDir "THIRD_PARTY_NOTICES.txt")

if (Test-Path $OutputZip) { Remove-Item $OutputZip }
Compress-Archive -Path $StageDir -DestinationPath $OutputZip
$hash = Get-FileHash -Path $OutputZip -Algorithm SHA256
$checksum = "$($hash.Hash.ToLowerInvariant())  $(Split-Path $OutputZip -Leaf)`n"
[IO.File]::WriteAllText("$OutputZip.sha256", $checksum, [Text.UTF8Encoding]::new($false))

Write-Host "Created $OutputZip"
Get-Content "$OutputZip.sha256"
