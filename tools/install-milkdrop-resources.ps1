<#
    .SYNOPSIS
        Installs recommended MilkDrop preset resources.
    .DESCRIPTION
        Installs the vendored preset packs and texture pack referenced in
        README.md into the expected foobar2000 profile folders:
        <profile>\milkdrop2\presets and <profile>\milkdrop2\textures.
    .EXAMPLE
        PS> .\tools\install-milkdrop-resources.ps1
    .EXAMPLE
        PS> .\tools\install-milkdrop-resources.ps1 -ProfilePath "$env:APPDATA\foobar2000-v2" -Force
    .INPUTS
        None.
    .OUTPUTS
        Preset and texture files in the target profile folder.
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
Add-Type -AssemblyName System.IO.Compression.FileSystem

function ConvertTo-ExtendedPath {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($env:OS -ne 'Windows_NT') {
        return $fullPath
    }

    if ($fullPath.StartsWith('\\?\')) {
        return $fullPath
    }

    if ($fullPath.StartsWith('\\')) {
        return '\\?\UNC\' + $fullPath.Substring(2)
    }

    return '\\?\' + $fullPath
}

function Test-SafeArchiveRelativePath {
    param(
        [Parameter(Mandatory)]
        [string] $RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $false
    }

    if ([System.IO.Path]::IsPathRooted($RelativePath) -or $RelativePath -match '^[A-Za-z]:') {
        return $false
    }

    foreach ($part in ($RelativePath -split '[\\/]')) {
        if ([string]::IsNullOrWhiteSpace($part) -or $part -eq '..') {
            return $false
        }
    }

    return $true
}

function Copy-ZipTreeContent {
    param(
        [Parameter(Mandatory)]
        [string] $ArchivePath,

        [Parameter()]
        [string] $SourceSubPath,

        [Parameter()]
        [string[]] $IncludeExtensions = @(),

        [Parameter(Mandatory)]
        [string] $DestinationPath,

        [Parameter(Mandatory)]
        [bool] $Overwrite
    )

    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        throw "Missing archive: $ArchivePath"
    }

    if (-not (Test-Path -LiteralPath $DestinationPath -PathType Container)) {
        $null = New-Item -ItemType Directory -Path $DestinationPath -Force
    }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $root = ($archive.Entries | Where-Object { -not [string]::IsNullOrWhiteSpace($_.FullName) } | Select-Object -First 1).FullName.Split('/')[0]
        if ([string]::IsNullOrWhiteSpace($root)) {
            throw "Could not locate archive root for $ArchivePath."
        }

        $sourcePrefix = if ([string]::IsNullOrWhiteSpace($SourceSubPath)) {
            "$root/"
        }
        else {
            "$root/$($SourceSubPath.Trim('/').Replace('\', '/'))/"
        }

        foreach ($entry in $archive.Entries) {
            if (-not $entry.FullName.StartsWith($sourcePrefix, [System.StringComparison]::Ordinal)) {
                continue
            }

            $relativePath = $entry.FullName.Substring($sourcePrefix.Length)
            if ([string]::IsNullOrWhiteSpace($relativePath)) {
                continue
            }

            $safeRelativePath = $relativePath.TrimEnd('/', '\')
            if (-not (Test-SafeArchiveRelativePath $safeRelativePath)) {
                throw "Archive entry uses an unsafe relative path: $($entry.FullName)"
            }

            $target = Join-Path $DestinationPath ($relativePath.Replace('/', [System.IO.Path]::DirectorySeparatorChar))
            if ($entry.FullName.EndsWith('/')) {
                [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $target)) | Out-Null
                continue
            }

            if ($IncludeExtensions.Count -gt 0) {
                $extension = [System.IO.Path]::GetExtension($relativePath)
                $extensionAllowed = $IncludeExtensions | Where-Object { [string]::Equals($_, $extension, [System.StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
                if (-not $extensionAllowed) {
                    continue
                }
            }

            $destinationRoot = [System.IO.Path]::GetFullPath($DestinationPath).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
            $targetFullPath = [System.IO.Path]::GetFullPath($target)
            if (-not $targetFullPath.StartsWith($destinationRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive entry escapes the destination path: $($entry.FullName)"
            }

            if ((-not $Overwrite) -and [System.IO.File]::Exists((ConvertTo-ExtendedPath $target))) {
                continue
            }

            $targetDirectory = Split-Path -Parent $target
            [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $targetDirectory)) | Out-Null

            $inputStream = $entry.Open()
            try {
                $outputStream = [System.IO.File]::Open((ConvertTo-ExtendedPath $target), [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
                try {
                    $inputStream.CopyTo($outputStream)
                }
                finally {
                    $outputStream.Dispose()
                }
            }
            finally {
                $inputStream.Dispose()
            }
        }
    }
    finally {
        $archive.Dispose()
    }
}

function Install-ResourcePack {
    param(
        [Parameter(Mandatory)]
        [hashtable] $Resource,

        [Parameter(Mandatory)]
        [bool] $Overwrite
    )

    $archivePath = $Resource.ArchivePath
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        Write-Host "INFO: Local archive missing for $($Resource.Name); downloading pinned archive..."
        Invoke-WebRequest -Uri $Resource.ArchiveUrl -OutFile $archivePath
    }

    Write-Host "INFO: Installing $($Resource.Name) to $($Resource.DestinationPath)..."
    $includeExtensions = if ($Resource.ContainsKey('IncludeExtensions')) { @($Resource.IncludeExtensions) } else { @() }
    Copy-ZipTreeContent -ArchivePath $archivePath -SourceSubPath $Resource.SourceSubPath -IncludeExtensions $includeExtensions -DestinationPath $Resource.DestinationPath -Overwrite $Overwrite
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

    Write-Host "INFO: Installing repaired preset additions to $DestinationPath..."
    foreach ($preset in Get-ChildItem -LiteralPath $fixedPresetPath -Filter '*.milk' -File) {
        Copy-Item -LiteralPath $preset.FullName -Destination (Join-Path $DestinationPath $preset.Name) -Force
    }
}

if (-not (Test-Path -LiteralPath $ProfilePath -PathType Container)) {
    throw "Profile path does not exist: $ProfilePath"
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resourceArchivePath = Join-Path $repositoryRoot 'third_party\milkdrop-resources'
$milkdropPath = Join-Path $ProfilePath 'milkdrop2'
$presetPath = Join-Path $milkdropPath 'presets'
$texturePath = Join-Path $milkdropPath 'textures'

$resources = @(
    @{
        Name            = 'cream-of-the-crop'
        ArchivePath     = Join-Path $resourceArchivePath 'presets-cream-of-the-crop-0180df21f5e0.zip'
        ArchiveUrl      = 'https://codeload.github.com/projectM-visualizer/presets-cream-of-the-crop/zip/0180df21f5e0bd39b9060cc5de420ed2f1f9e509'
        SourceSubPath   = ''
        IncludeExtensions = @('.milk')
        DestinationPath = $presetPath
    },
    @{
        Name            = 'milkdrop-original'
        ArchivePath     = Join-Path $resourceArchivePath 'presets-milkdrop-original-e03b83e3338d.zip'
        ArchiveUrl      = 'https://codeload.github.com/projectM-visualizer/presets-milkdrop-original/zip/e03b83e3338d8f1ed6cbcf908c719f249ef24288'
        SourceSubPath   = 'Milkdrop-Original'
        IncludeExtensions = @('.milk')
        DestinationPath = $presetPath
    },
    @{
        Name            = 'milkdrop-textures'
        ArchivePath     = Join-Path $resourceArchivePath 'presets-milkdrop-texture-pack-ff8edf2a8fa0.zip'
        ArchiveUrl      = 'https://codeload.github.com/projectM-visualizer/presets-milkdrop-texture-pack/zip/ff8edf2a8fa07e55ad562f1af97076526c484f7d'
        SourceSubPath   = 'textures'
        IncludeExtensions = @('.bmp', '.jpg', '.jpeg', '.png', '.tga')
        DestinationPath = $texturePath
    }
)

Write-Host "INFO: Installing MilkDrop resources into $milkdropPath"

if (-not $PSCmdlet.ShouldProcess($milkdropPath, 'Install recommended MilkDrop preset resources')) {
    return
}

$null = New-Item -ItemType Directory -Path $presetPath -Force
$null = New-Item -ItemType Directory -Path $texturePath -Force
$null = New-Item -ItemType Directory -Path $resourceArchivePath -Force

foreach ($resource in $resources) {
    Install-ResourcePack -Resource $resource -Overwrite $Force.IsPresent
}
Install-FixedPresetPack -RepositoryRoot $repositoryRoot -DestinationPath $presetPath

Write-Host 'INFO: Done.'
Write-Host "INFO: Presets installed to $presetPath"
Write-Host "INFO: Textures installed to $texturePath"
