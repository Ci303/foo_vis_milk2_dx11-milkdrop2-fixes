<#
    .SYNOPSIS
        Installs the foobar2000 MilkDrop component and bundled resources.
    .DESCRIPTION
        Installs foo_vis_milk2 into the foobar2000 v2 x64 profile component
        directory, then installs the bundled MilkDrop presets and textures.
    .EXAMPLE
        PS> .\tools\install-milkdrop-bundle.ps1
    .EXAMPLE
        PS> .\tools\install-milkdrop-bundle.ps1 -ProfilePath "$env:APPDATA\foobar2000-v2" -Force
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(HelpMessage = 'Path to the foobar2000 profile directory.')]
    [string] $ProfilePath = (Join-Path $env:APPDATA 'foobar2000-v2'),

    [Parameter(HelpMessage = 'Overwrite existing preset and texture files.')]
    [switch] $Force,

    [Parameter(HelpMessage = 'Do not wait for Enter before exiting. Intended for tests and automation.')]
    [switch] $NoPause,

    [Parameter(HelpMessage = 'Skip the foobar2000 running-process check. Intended for isolated test profiles only.')]
    [switch] $SkipFoobarCheck,

    [Parameter(HelpMessage = 'Skip the installed foobar2000 x64 check. Intended for portable/test profiles only.')]
    [switch] $SkipFoobarInstallCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$foobarDownloadUrl = 'https://www.foobar2000.org/download'

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

function Copy-ZipEntryToFile {
    param(
        [Parameter(Mandatory)]
        [System.IO.Compression.ZipArchiveEntry] $Entry,

        [Parameter(Mandatory)]
        [string] $DestinationPath
    )

    $destinationDirectory = Split-Path -Parent $DestinationPath
    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $destinationDirectory)) | Out-Null

    $inputStream = $Entry.Open()
    try {
        $outputStream = [System.IO.File]::Open((ConvertTo-ExtendedPath $DestinationPath), [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
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

function Install-ComponentPackage {
    param(
        [Parameter(Mandatory)]
        [string] $PackagePath,

        [Parameter(Mandatory)]
        [string] $ProfilePath
    )

    if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
        throw "Missing component package: $PackagePath"
    }

    $componentDirectory = Join-Path $ProfilePath 'user-components-x64\foo_vis_milk2'
    $dataDirectory = Join-Path $componentDirectory 'data'
    $backupDirectory = Join-Path $componentDirectory 'backup'
    $targetDll = Join-Path $componentDirectory 'foo_vis_milk2.dll'

    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $componentDirectory)) | Out-Null
    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $dataDirectory)) | Out-Null
    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $backupDirectory)) | Out-Null

    if (Test-Path -LiteralPath $targetDll -PathType Leaf) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        Copy-Item -LiteralPath $targetDll -Destination (Join-Path $backupDirectory "foo_vis_milk2.dll.bak-$stamp") -Force
    }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($PackagePath)
    try {
        $dllEntry = $archive.GetEntry('x64/foo_vis_milk2.dll')
        if (-not $dllEntry) {
            throw "Component package does not contain x64/foo_vis_milk2.dll."
        }
        Copy-ZipEntryToFile -Entry $dllEntry -DestinationPath $targetDll

        foreach ($entry in $archive.Entries) {
            if (-not $entry.FullName.StartsWith('data/', [System.StringComparison]::Ordinal)) {
                continue
            }

            if ($entry.FullName.EndsWith('/')) {
                continue
            }

            $relativePath = $entry.FullName.Substring('data/'.Length).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
            Copy-ZipEntryToFile -Entry $entry -DestinationPath (Join-Path $dataDirectory $relativePath)
        }
    }
    finally {
        $archive.Dispose()
    }

    Write-Host "INFO: Component installed to $componentDirectory"
}

function Test-WindowsExecutableX64 {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        try {
            if ($stream.Length -lt 0x40) {
                return $false
            }

            $stream.Position = 0x3c
            $peOffset = $reader.ReadInt32()
            if ($peOffset -lt 0 -or $peOffset + 6 -gt $stream.Length) {
                return $false
            }

            $stream.Position = $peOffset
            $signature = $reader.ReadUInt32()
            if ($signature -ne 0x00004550) {
                return $false
            }

            $machine = $reader.ReadUInt16()
            return $machine -eq 0x8664
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-Foobar2000CandidatePaths {
    $paths = New-Object System.Collections.Generic.List[string]

    foreach ($basePath in @(
        [Environment]::GetFolderPath('ProgramFiles'),
        [Environment]::GetFolderPath('ProgramFilesX86'),
        (Join-Path $env:LOCALAPPDATA 'Programs')
    )) {
        if (-not [string]::IsNullOrWhiteSpace($basePath)) {
            $paths.Add((Join-Path $basePath 'foobar2000\foobar2000.exe'))
        }
    }

    foreach ($registryPath in @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )) {
        Get-ItemProperty -Path $registryPath -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -like 'foobar2000*' } |
            ForEach-Object {
                if (-not [string]::IsNullOrWhiteSpace($_.InstallLocation)) {
                    $paths.Add((Join-Path $_.InstallLocation 'foobar2000.exe'))
                }

                if (-not [string]::IsNullOrWhiteSpace($_.DisplayIcon)) {
                    $displayIcon = ($_.DisplayIcon -replace ',\d+$', '').Trim('"')
                    if ([System.IO.Path]::GetFileName($displayIcon) -ieq 'foobar2000.exe') {
                        $paths.Add($displayIcon)
                    }
                }
            }
    }

    return $paths | Select-Object -Unique
}

function Test-Foobar2000X64Installed {
    foreach ($candidatePath in Get-Foobar2000CandidatePaths) {
        if (Test-WindowsExecutableX64 $candidatePath) {
            return $true
        }
    }

    return $false
}

function Install-StarterTemplates {
    param(
        [Parameter(Mandatory)]
        [string] $RepositoryRoot,

        [Parameter(Mandatory)]
        [string] $ProfilePath
    )

    $templateSource = Join-Path $RepositoryRoot 'templates'
    if (-not (Test-Path -LiteralPath $templateSource -PathType Container)) {
        return
    }

    $templateDestination = Join-Path $ProfilePath 'milkdrop2\templates'
    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $templateDestination)) | Out-Null

    Get-ChildItem -LiteralPath $templateSource -File -Filter '*.fth' |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $templateDestination $_.Name) -Force
        }

    Write-Host "INFO: Starter foobar2000 template copied to $templateDestination"
}

function Install-BlacklistScannerScripts {
    param(
        [Parameter(Mandatory)]
        [string] $RepositoryRoot,

        [Parameter(Mandatory)]
        [string] $ProfilePath
    )

    $scriptDestination = Join-Path $ProfilePath 'milkdrop2'
    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $scriptDestination)) | Out-Null

    foreach ($scriptName in @('refresh_milkdrop2_blacklist.ps1', 'refresh_milkdrop2_blacklist.cmd', 'switch_milkdrop_profile.ps1')) {
        $sourcePath = Join-Path (Join-Path $RepositoryRoot 'tools') $scriptName
        if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
            Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $scriptDestination $scriptName) -Force
        }
    }

    Write-Host "INFO: Preset blacklist scanner scripts copied to $scriptDestination"
}

function Stop-WithMessage {
    param(
        [Parameter(Mandatory)]
        [string] $Message,

        [Parameter(Mandatory)]
        [int] $ExitCode
    )

    Write-Host "ERROR: $Message" -ForegroundColor Red
    if ((-not $NoPause.IsPresent) -and $Host.Name -eq 'ConsoleHost') {
        Read-Host 'Press Enter to exit' | Out-Null
    }

    exit $ExitCode
}

if (-not $SkipFoobarCheck.IsPresent) {
    $runningFoobar = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'foobar2000*' }
    if ($runningFoobar) {
        Stop-WithMessage -Message 'foobar2000 is running. Close foobar2000 and run this installer again.' -ExitCode 2
    }
}

if (-not $SkipFoobarInstallCheck.IsPresent) {
    if (-not (Test-Foobar2000X64Installed)) {
        Write-Host 'ERROR: foobar2000 x64 does not appear to be installed.' -ForegroundColor Red
        Write-Host ''
        Write-Host 'This MilkDrop bundle is an add-on for foobar2000 v2 x64.'
        Write-Host 'Install foobar2000 first, close it after installation, then run this MilkDrop installer again.'
        Write-Host ''
        Write-Host "INFO: Opening official foobar2000 download page: $foobarDownloadUrl"
        Start-Process $foobarDownloadUrl
        if ((-not $NoPause.IsPresent) -and $Host.Name -eq 'ConsoleHost') {
            Read-Host 'Install foobar2000 x64, then run this installer again. Press Enter to exit' | Out-Null
        }
        exit 3
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$packagePath = Join-Path $repositoryRoot 'foo_vis_milk2.fb2k-component'
$resourceInstallerPath = Join-Path $PSScriptRoot 'install-milkdrop-resources.ps1'

if (-not (Test-Path -LiteralPath $ProfilePath -PathType Container)) {
    [System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedPath $ProfilePath)) | Out-Null
}

if (-not $PSCmdlet.ShouldProcess($ProfilePath, 'Install MilkDrop component, presets, and textures')) {
    return
}

Install-ComponentPackage -PackagePath $packagePath -ProfilePath $ProfilePath
& $resourceInstallerPath -ProfilePath $ProfilePath -Force:$Force.IsPresent
Install-StarterTemplates -RepositoryRoot $repositoryRoot -ProfilePath $ProfilePath
Install-BlacklistScannerScripts -RepositoryRoot $repositoryRoot -ProfilePath $ProfilePath

Write-Host ''
Write-Host 'MilkDrop bundle install complete.'
Write-Host 'Restart foobar2000, then add or open the MilkDrop visualisation.'
Write-Host 'Optional starter template: import milkdrop2\templates\foobar2000-milkdrop-starter.fth from the foobar2000 profile folder.'
Write-Host 'Preset blacklist scanner: use Preferences > MilkDrop 2 > Preset Blacklist > Scan.'

if ((-not $NoPause.IsPresent) -and $Host.Name -eq 'ConsoleHost') {
    Read-Host 'Press Enter to exit' | Out-Null
}
