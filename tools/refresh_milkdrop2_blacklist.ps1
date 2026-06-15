param(
  [ValidateSet("high", "ultra", "md3", "md3_safe", "auto")][string]$Profile = "auto",
  [int]$BadPresetStrikeThreshold = 2,
  [int]$ShaderErrorMinCount = 3,
  [int]$CrashWindowSeconds = 300,
  [int]$DryRunLimit = 40,
[switch]$DryRun,
[switch]$FullDryRun,
[switch]$NoProfileSwitch = $true,
[switch]$ResetSafetyWindow,
[switch]$PruneStaleBlacklist,
[string]$MilkdropRoot
)

function Resolve-DefaultMilkdropRoot {
  $appDataRoot = $null
  if ($env:APPDATA) {
    $appDataRoot = Join-Path (Join-Path $env:APPDATA 'foobar2000-v2') 'milkdrop2'
    if (Test-Path -LiteralPath $appDataRoot) {
      return $appDataRoot
    }
  }

  $scriptDir = Split-Path -Parent $PSCommandPath
  if ((Test-Path -LiteralPath (Join-Path $scriptDir 'switch_milkdrop_profile.ps1')) -and
      ((Test-Path -LiteralPath (Join-Path $scriptDir 'presets')) -or
       (Test-Path -LiteralPath (Join-Path $scriptDir 'milk2.ini')) -or
       (Test-Path -LiteralPath (Join-Path $scriptDir 'preset-blacklist.txt')))) {
    return $scriptDir
  }

  $siblingRoot = Join-Path $scriptDir 'milkdrop2'
  if (Test-Path -LiteralPath (Join-Path $siblingRoot 'switch_milkdrop_profile.ps1')) {
    return $siblingRoot
  }

  return $appDataRoot
}
if ([string]::IsNullOrWhiteSpace($MilkdropRoot)) {
  $MilkdropRoot = Resolve-DefaultMilkdropRoot
}
if (-not (Test-Path $MilkdropRoot)) {
  throw "Milkdrop root not found: $MilkdropRoot"
}

$runner = Join-Path $MilkdropRoot "switch_milkdrop_profile.ps1"
if (-not (Test-Path $runner)) {
  throw "Runner script not found: $runner"
}

$resetMarker = Join-Path $MilkdropRoot "reset-runtime-safety-cache.txt"
$createdResetMarker = $false
if ($ResetSafetyWindow) {
  if (-not (Test-Path $resetMarker)) {
    Set-Content -Path $resetMarker -Value ""
    $createdResetMarker = $true
  }
}

$runnerArgs = @{
  MilkdropRoot = $MilkdropRoot
  Profile = $Profile
  BadPresetStrikeThreshold = $BadPresetStrikeThreshold
  ShaderErrorMinCount = $ShaderErrorMinCount
  CrashWindowSeconds = $CrashWindowSeconds
  SkipMd3Presets = $true
  NoProfileSwitch = $true
}
if ($DryRun) {
  $runnerArgs.DryRun = $true
  $runnerArgs.DryRunLimit = $DryRunLimit
  if ($FullDryRun) { $runnerArgs.FullDryRun = $true }
}
if ($PruneStaleBlacklist) {
  $runnerArgs.PruneStaleBlacklist = $true
}

& $runner @runnerArgs

if ($createdResetMarker -and (Test-Path $resetMarker)) {
  Remove-Item -LiteralPath $resetMarker -Force
}
