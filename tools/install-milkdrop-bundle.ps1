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

function Test-Foobar2000X64Installed {
    $programFiles = [Environment]::GetFolderPath('ProgramFiles')
    if ([string]::IsNullOrWhiteSpace($programFiles)) {
        return $false
    }

    $foobarPath = Join-Path $programFiles 'foobar2000\foobar2000.exe'
    return Test-Path -LiteralPath $foobarPath -PathType Leaf
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

Write-Host ''
Write-Host 'MilkDrop bundle install complete.'
Write-Host 'Restart foobar2000, then add or open the MilkDrop visualisation.'

if ((-not $NoPause.IsPresent) -and $Host.Name -eq 'ConsoleHost') {
    Read-Host 'Press Enter to exit' | Out-Null
}
