<#
    .SYNOPSIS
        Downloads and installs recommended MilkDrop preset resources.
    .DESCRIPTION
        Fetches the preset packs and texture pack referenced in README.md and
        installs them into the expected foobar2000 profile folders:
        <profile>\milkdrop2\presets and <profile>\milkdrop2\textures.
    .EXAMPLE
        PS> .\tools\install-milkdrop-resources.ps1
    .EXAMPLE
        PS> .\tools\install-milkdrop-resources.ps1 -ProfilePath "$env:APPDATA\foobar2000-v2" -Force
    .INPUTS
        None.
    .OUTPUTS
        Downloaded preset and texture files in the target profile folder.
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(HelpMessage = 'Path to the foobar2000 profile directory.')]
    [string] $ProfilePath = (Join-Path $env:APPDATA 'foobar2000-v2'),

    [Parameter(HelpMessage = 'Overwrite files that already exist in the destination.')]
    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RepoArchiveUrl {
    param(
        [Parameter(Mandatory)]
        [string] $Repository
    )

    $repoInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repository"
    $branch = $repoInfo.default_branch
    return @{
        Branch = $branch
        Url    = "https://codeload.github.com/$Repository/zip/refs/heads/$branch"
    }
}

function Copy-TreeContent {
    param(
        [Parameter(Mandatory)]
        [string] $SourcePath,

        [Parameter(Mandatory)]
        [string] $DestinationPath,

        [Parameter(Mandatory)]
        [bool] $Overwrite
    )

    if (-not (Test-Path -LiteralPath $SourcePath -PathType Container)) {
        throw "Missing extracted source path: $SourcePath"
    }

    if (-not (Test-Path -LiteralPath $DestinationPath -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $DestinationPath -Force
    }

    foreach ($item in Get-ChildItem -LiteralPath $SourcePath -Force) {
        $target = Join-Path $DestinationPath $item.Name

        if ($item.PSIsContainer) {
            Copy-Item -LiteralPath $item.FullName -Destination $target -Recurse -Force
            continue
        }

        if ((-not $Overwrite) -and (Test-Path -LiteralPath $target -PathType Leaf)) {
            continue
        }

        Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }
}

function Install-ResourcePack {
    param(
        [Parameter(Mandatory)]
        [hashtable] $Resource,

        [Parameter(Mandatory)]
        [string] $WorkingPath,

        [Parameter(Mandatory)]
        [bool] $Overwrite
    )

    $archiveInfo = Get-RepoArchiveUrl -Repository $Resource.Repository
    $zipPath = Join-Path $WorkingPath ($Resource.Name + '.zip')
    $extractPath = Join-Path $WorkingPath ($Resource.Name + '-extract')

    Write-Host "INFO: Downloading $($Resource.Repository) ($($archiveInfo.Branch))..."
    Invoke-WebRequest -Uri $archiveInfo.Url -OutFile $zipPath

    Write-Host "INFO: Extracting $($Resource.Name)..."
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractPath -Force

    $archiveRoot = Get-ChildItem -LiteralPath $extractPath -Directory | Select-Object -First 1
    if (-not $archiveRoot) {
        throw "Could not locate extracted archive root for $($Resource.Name)."
    }

    $sourcePath = if ([string]::IsNullOrWhiteSpace($Resource.SourceSubPath)) {
        $archiveRoot.FullName
    }
    else {
        Join-Path $archiveRoot.FullName $Resource.SourceSubPath
    }

    Write-Host "INFO: Installing $($Resource.Name) to $($Resource.DestinationPath)..."
    Copy-TreeContent -SourcePath $sourcePath -DestinationPath $Resource.DestinationPath -Overwrite $Overwrite
}

function Install-FixedPresetPack {
    param(
        [Parameter(Mandatory)]
        [string] $RepositoryRoot,

        [Parameter(Mandatory)]
        [string] $DestinationPath
    )

    $fixedPresetPath = Join-Path $RepositoryRoot 'presets\fixed-blacklisted'
    if (-not (Test-Path -LiteralPath $fixedPresetPath -PathType Container)) {
        throw "Missing fixed preset pack: $fixedPresetPath"
    }

    Write-Host "INFO: Installing fixed blacklist presets to $DestinationPath..."
    foreach ($preset in Get-ChildItem -LiteralPath $fixedPresetPath -Filter '*.milk' -File) {
        Copy-Item -LiteralPath $preset.FullName -Destination (Join-Path $DestinationPath $preset.Name) -Force
    }
}

if (-not (Test-Path -LiteralPath $ProfilePath -PathType Container)) {
    throw "Profile path does not exist: $ProfilePath"
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$milkdropPath = Join-Path $ProfilePath 'milkdrop2'
$presetPath = Join-Path $milkdropPath 'presets'
$texturePath = Join-Path $milkdropPath 'textures'

$resources = @(
    @{
        Name            = 'cream-of-the-crop'
        Repository      = 'projectM-visualizer/presets-cream-of-the-crop'
        SourceSubPath   = ''
        DestinationPath = $presetPath
    },
    @{
        Name            = 'milkdrop-original'
        Repository      = 'projectM-visualizer/presets-milkdrop-original'
        SourceSubPath   = 'Milkdrop-Original'
        DestinationPath = $presetPath
    },
    @{
        Name            = 'milkdrop-textures'
        Repository      = 'projectM-visualizer/presets-milkdrop-texture-pack'
        SourceSubPath   = 'textures'
        DestinationPath = $texturePath
    }
)

Write-Host "INFO: Installing MilkDrop resources into $milkdropPath"

if (-not $PSCmdlet.ShouldProcess($milkdropPath, 'Download and install recommended MilkDrop preset resources')) {
    return
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('milkdrop-resource-install-' + [System.Guid]::NewGuid().ToString('N'))

try {
    $null = New-Item -ItemType Directory -Path $tempRoot -Force
    $null = New-Item -ItemType Directory -Path $presetPath -Force
    $null = New-Item -ItemType Directory -Path $texturePath -Force

    foreach ($resource in $resources) {
        Install-ResourcePack -Resource $resource -WorkingPath $tempRoot -Overwrite $Force.IsPresent
    }
    Install-FixedPresetPack -RepositoryRoot $repositoryRoot -DestinationPath $presetPath

    Write-Host 'INFO: Done.'
    Write-Host "INFO: Presets installed to $presetPath"
    Write-Host "INFO: Textures installed to $texturePath"
}
finally {
    if (Test-Path -LiteralPath $tempRoot -PathType Container) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
