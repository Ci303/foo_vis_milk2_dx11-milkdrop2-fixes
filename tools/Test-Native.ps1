param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [switch]$SkipSolutionBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$solutionPath = Join-Path $repositoryRoot 'foo_vis_milk2.sln'
$testProjectPath = Join-Path $repositoryRoot 'test\test.vcxproj'
$sharedProjectPath = Join-Path $repositoryRoot 'external\foobar2000\shared\shared.vcxproj'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

$msbuildPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' |
    Select-Object -First 1
$vstestPath = & $vswherePath -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' |
    Select-Object -First 1

if (-not $msbuildPath -or -not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
    throw 'MSBuild with the Visual C++ tools was not found.'
}
if (-not $vstestPath -or -not (Test-Path -LiteralPath $vstestPath -PathType Leaf)) {
    throw 'VSTest.Console.exe was not found.'
}

$solutionDirectoryProperty = '/p:SolutionDir=' + $repositoryRoot.TrimEnd('\') + '\'
$buildProperties = @(
    '/m',
    '/t:Build',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:PlatformToolset=v145',
    $solutionDirectoryProperty,
    '/v:minimal'
)

if (-not $SkipSolutionBuild) {
    & $msbuildPath $solutionPath @buildProperties
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

& $msbuildPath $sharedProjectPath @buildProperties
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$sharedOutput = @(
    (Join-Path $repositoryRoot "Bin\$Platform\$Configuration\shared.dll"),
    (Join-Path $repositoryRoot "$Platform\$Configuration\shared.dll"),
    (Join-Path $repositoryRoot "$Configuration\shared.dll")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $sharedOutput) {
    throw 'The foobar2000 shared-runtime build did not produce shared.dll.'
}

$testProjectText = Get-Content -LiteralPath $testProjectPath -Raw
if ($testProjectText.Contains('$(SolutionDir)data\shared')) {
    $stagingDirectory = Join-Path $repositoryRoot 'data\shared'
    New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
    Copy-Item -LiteralPath $sharedOutput -Destination (Join-Path $stagingDirectory "shared-$Platform.dll") -Force
}

& $msbuildPath $testProjectPath @buildProperties
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$testAssembly = Join-Path $repositoryRoot "Bin\$Platform\$Configuration\test.dll"
if (-not (Test-Path -LiteralPath $testAssembly -PathType Leaf)) {
    throw "The native test build did not produce $testAssembly."
}

$sharedTestPath = Join-Path ([System.IO.Path]::GetDirectoryName($testAssembly)) 'shared.dll'
if (-not [string]::Equals(
        [System.IO.Path]::GetFullPath($sharedOutput),
        [System.IO.Path]::GetFullPath($sharedTestPath),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    Copy-Item -LiteralPath $sharedOutput -Destination $sharedTestPath -Force
}

& $vstestPath "/Platform:$Platform" $testAssembly
exit $LASTEXITCODE
