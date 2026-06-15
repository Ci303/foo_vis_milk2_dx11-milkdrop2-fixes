<#
    .SYNOPSIS
        Builds a self-extracting MilkDrop bundle installer.
    .DESCRIPTION
        Creates a single EXE containing the foobar2000 component package,
        vendored preset/texture archives, repaired preset additions, and installer
        scripts.
    .EXAMPLE
        PS> .\tools\build-milkdrop-installer.ps1
#>

[CmdletBinding()]
param(
    [Parameter(HelpMessage = 'Installer version used in the output filename.')]
    [string] $Version = '',

    [Parameter(HelpMessage = 'Output directory for the installer EXE.')]
    [string] $OutputPath = (Join-Path (Get-Location) 'dist'),

    [Parameter(HelpMessage = 'Path to the component package to embed. Defaults to foo_vis_milk2-$Version.fb2k-component.')]
    [string] $PackagePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Copy-RequiredItem {
    param(
        [Parameter(Mandatory)]
        [string] $SourcePath,

        [Parameter(Mandatory)]
        [string] $DestinationPath
    )

    if (-not (Test-Path -LiteralPath $SourcePath)) {
        throw "Missing required installer input: $SourcePath"
    }

    $destinationDirectory = Split-Path -Parent $DestinationPath
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $destinationDirectory -Force
    }

    Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Recurse -Force
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sevenZip = (Get-Command 7z.exe -ErrorAction Stop).Source
$sevenZipVersionText = (& $sevenZip | Select-Object -First 2) -join "`n"
if ($sevenZipVersionText -notmatch '7-Zip\s+(\d+)\.(\d+)') {
    throw "Could not determine 7-Zip version from $sevenZip."
}

$sevenZipVersion = [version]::new([int]$Matches[1], [int]$Matches[2])
if ($sevenZipVersion -lt [version]::new(24, 0)) {
    throw "7-Zip 24.00 or newer is required to build the installer. Found $sevenZipVersion at $sevenZip."
}

$sevenZipSfx = Join-Path (Split-Path -Parent $sevenZip) '7z.sfx'
if (-not (Test-Path -LiteralPath $sevenZipSfx -PathType Leaf)) {
    throw "Could not locate 7z.sfx next to $sevenZip."
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionHeader = Join-Path $repositoryRoot 'foo_vis_milk2\version.h'
    $versionText = Get-Content -LiteralPath $versionHeader -Raw
    $major = [regex]::Match($versionText, '#define\s+APPLICATION_VERSION_MAJOR\s+(\d+)').Groups[1].Value
    $minor = [regex]::Match($versionText, '#define\s+APPLICATION_VERSION_MINOR\s+(\d+)').Groups[1].Value
    $build = [regex]::Match($versionText, '#define\s+APPLICATION_VERSION_BUILD\s+(\d+)').Groups[1].Value
    $revision = [regex]::Match($versionText, '#define\s+APPLICATION_VERSION_REVISION\s+(-?\d+)').Groups[1].Value
    $Version = "$major.$minor.$build.$revision"
}

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $repositoryRoot "foo_vis_milk2-$Version.fb2k-component"
}

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Build the matching component package first: $PackagePath"
}

$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('milkdrop-installer-staging-' + [System.Guid]::NewGuid().ToString('N'))
$payloadArchive = Join-Path $stagingRoot 'payload.7z'
$configPath = Join-Path $stagingRoot 'sfx-config.txt'
$outputFileName = "foo_vis_milk2-$Version-installer.exe"
$outputFilePath = Join-Path $OutputPath $outputFileName

try {
    $payloadRoot = Join-Path $stagingRoot 'payload'
    $null = New-Item -ItemType Directory -Path $payloadRoot -Force
    $null = New-Item -ItemType Directory -Path $OutputPath -Force

    Copy-RequiredItem -SourcePath $PackagePath -DestinationPath (Join-Path $payloadRoot 'foo_vis_milk2.fb2k-component')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'tools\install-milkdrop-bundle.ps1') -DestinationPath (Join-Path $payloadRoot 'tools\install-milkdrop-bundle.ps1')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'tools\install-milkdrop-resources.ps1') -DestinationPath (Join-Path $payloadRoot 'tools\install-milkdrop-resources.ps1')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'tools\refresh_milkdrop2_blacklist.ps1') -DestinationPath (Join-Path $payloadRoot 'tools\refresh_milkdrop2_blacklist.ps1')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'tools\refresh_milkdrop2_blacklist.cmd') -DestinationPath (Join-Path $payloadRoot 'tools\refresh_milkdrop2_blacklist.cmd')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'tools\switch_milkdrop_profile.ps1') -DestinationPath (Join-Path $payloadRoot 'tools\switch_milkdrop_profile.ps1')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'presets\fixed-blacklisted') -DestinationPath (Join-Path $payloadRoot 'presets\fixed-blacklisted')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'third_party\milkdrop-resources') -DestinationPath (Join-Path $payloadRoot 'third_party\milkdrop-resources')
    Copy-RequiredItem -SourcePath (Join-Path $repositoryRoot 'templates') -DestinationPath (Join-Path $payloadRoot 'templates')

    Push-Location $payloadRoot
    try {
        & $sevenZip a -t7z -mx=9 $payloadArchive * | Out-Host
    }
    finally {
        Pop-Location
    }

    @'
;!@Install@!UTF-8!
Title="foo_vis_milk2 MilkDrop Bundle Installer"
BeginPrompt="This installs the foo_vis_milk2 component, MilkDrop presets, and textures into your foobar2000 v2 x64 profile. Close foobar2000 before continuing."
RunProgram="powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\\install-milkdrop-bundle.ps1"
;!@InstallEnd@!
'@ | Set-Content -LiteralPath $configPath -Encoding UTF8

    $outputStream = [System.IO.File]::Create($outputFilePath)
    try {
        foreach ($part in @($sevenZipSfx, $configPath, $payloadArchive)) {
            $inputStream = [System.IO.File]::OpenRead($part)
            try {
                $inputStream.CopyTo($outputStream)
            }
            finally {
                $inputStream.Dispose()
            }
        }
    }
    finally {
        $outputStream.Dispose()
    }

    Write-Host "INFO: Created installer $outputFilePath"
}
finally {
    if (Test-Path -LiteralPath $stagingRoot -PathType Container) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
