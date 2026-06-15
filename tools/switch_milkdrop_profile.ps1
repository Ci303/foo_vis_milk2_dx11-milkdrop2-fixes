param(
  [string]$MilkdropRoot,
  [ValidateSet("high", "ultra", "md3", "md3_safe", "auto")][string]$Profile = "auto",
  [ValidateSet("high", "ultra", "md3", "md3_safe")][string]$FallbackProfile = "md3_safe",
  [int]$CrashWindowSeconds = 300,
  [int]$BadPresetStrikeThreshold = 2,
  [int]$ShaderErrorMinCount = 3,
  [switch]$SkipMd3Presets = $true,
  [switch]$DryRun,
  [switch]$FullDryRun,
  [int]$DryRunLimit = 40,
  [switch]$IncludeNonBlacklistOnly,
  [switch]$PruneStaleBlacklist,
  [switch]$NoProfileSwitch
)

$knownProfiles = @('high', 'ultra', 'md3', 'md3_safe', 'auto')
if (-not [string]::IsNullOrWhiteSpace($MilkdropRoot) -and $knownProfiles -contains $MilkdropRoot -and -not (Test-Path -LiteralPath $MilkdropRoot)) {
  if (-not $PSBoundParameters.ContainsKey('Profile')) {
    $Profile = $MilkdropRoot
  }
  $MilkdropRoot = $null
}

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
  if (Test-Path -LiteralPath $siblingRoot) {
    return $siblingRoot
  }

  return $appDataRoot
}

if ([string]::IsNullOrWhiteSpace($MilkdropRoot)) {
  $root = Resolve-DefaultMilkdropRoot
}
else {
  $root = $MilkdropRoot
}

if (-not (Test-Path $root)) {
  throw "Milkdrop root not found: $root"
}
$modePath = Join-Path $root 'milk2_profile.mode'
$logPath = Join-Path $root 'runtime-safety-cache.txt'
$blacklistPath = Join-Path $root 'preset-blacklist.txt'
$managedBlacklistPath = Join-Path $root 'preset-blacklist.scanner-managed.txt'
$scanCachePath = Join-Path $root 'preset-scan-cache.txt'
$shaderLogPath = Join-Path $root 'shader-debug.log'
$resetSafetyCacheMarker = Join-Path $root 'reset-runtime-safety-cache.txt'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$ignorePatterns = @(
  '*idle*oscilloscope*.milk',
  '*idle*oscope*.milk'
)

$skipMd3Patterns = @(
  '*\md3\*',
  '*\md3_*',
  '*\md3-*',
  '*\md3unique*',
  '*\md3 unique*',
  '*\md3-unique*',
  '*\md3_reference*',
  '*\md3_*\*',
  'md3*',
  'unique_*.milk'
)

function Is-IgnoredPreset([string]$path, [string[]]$ignorePatterns, [string[]]$skipMd3Patterns, [bool]$skipMd3Presets) {
  if (-not $path) {
    return $false
  }

  $candidate = $path.Trim()
  $name = Split-Path -Leaf $candidate
  if (-not $name) {
    return $false
  }

  foreach ($pattern in $ignorePatterns) {
    if (($name -like $pattern) -or ($candidate -like $pattern)) {
      return $true
    }
  }
  if (-not $skipMd3Presets) {
    return $false
  }
  foreach ($pattern in $skipMd3Patterns) {
    if (($name -like $pattern) -or ($candidate -like $pattern)) {
      return $true
    }
  }
  return $false
}

function Add-IfExists([string]$path, [System.Collections.Generic.HashSet[string]]$set, [string[]]$ignorePatterns, [string[]]$skipMd3Patterns, [bool]$skipMd3Presets) {
  if ([string]::IsNullOrWhiteSpace($path)) {
    return
  }

  $name = Split-Path -Leaf $path
  if ([string]::IsNullOrWhiteSpace($name)) {
    return
  }
  if (Is-IgnoredPreset -path $path -ignorePatterns $ignorePatterns -skipMd3Patterns $skipMd3Patterns -skipMd3Presets $skipMd3Presets) {
    return
  }

  [void]$set.Add($name)
}

function Import-PresetScanCache([string]$path, [System.Collections.Generic.HashSet[string]]$set, [string[]]$ignorePatterns) {
  if (-not (Test-Path $path)) {
    return
  }

  foreach ($line in Get-Content -Path $path) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) {
      continue
    }

    $parts = $line -split "`t"
    if ($parts.Count -lt 6) {
      continue
    }

    if ($parts[5].Trim() -ne '0') {
      continue
    }

    Add-IfExists -path $parts[0].Trim() -set $set -ignorePatterns $ignorePatterns -skipMd3Patterns $skipMd3Patterns -skipMd3Presets $SkipMd3Presets
  }
}

function Import-ShaderFallbackCache([string]$path, [int]$threshold, [System.Collections.Generic.HashSet[string]]$set, [string[]]$ignorePatterns) {
  if (-not (Test-Path $path)) {
    return
  }

  foreach ($line in Get-Content -Path $path) {
    if ($line -match '^(?<type>[^\t]+)\t(?<count>\d+)\t(?<preset>.+\.milk)$') {
      $type = $matches['type']
      $count = [int]$matches['count']
      $effectiveThreshold = if ($type -eq 'visual-inactive') { 1 } else { $threshold }
      if ($count -ge $effectiveThreshold -and $type -in @('slow-load', 'shader-fallback', 'visual-inactive')) {
        Add-IfExists -path $matches['preset'] -set $set -ignorePatterns $ignorePatterns -skipMd3Patterns $skipMd3Patterns -skipMd3Presets $SkipMd3Presets
      }
    }
  }
}

function Import-ShaderDebugFailures([string]$path, [int]$threshold, [int]$lookbackDays, [System.Collections.Generic.HashSet[string]]$set, [string[]]$ignorePatterns) {
  if (-not (Test-Path $path)) {
    return
  }

  $lookup = @{}
  $cutoff = (Get-Date).AddDays(-[double]$lookbackDays)

  foreach ($line in Get-Content -Path $path) {
    if (-not ($line -match '^(?<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})')) {
      continue
    }

    $lineTimestamp = [DateTime]::ParseExact($matches['ts'], 'yyyy-MM-dd HH:mm:ss.fff', $null)
    if ($lineTimestamp -lt $cutoff) {
      continue
    }

    if (-not ($line -match 'preset="(?<preset>[^"]+\.milk)"')) {
      continue
    }

    $preset = $matches['preset']
    if (-not ($line -match 'fallback shader|using fallback shader|d3d compile failed|pixel shader load failed')) {
      continue
    }

    if (-not $lookup.ContainsKey($preset)) {
      $lookup[$preset] = 0
    }
    $lookup[$preset] += 1
  }

  foreach ($preset in $lookup.Keys) {
    if ($lookup[$preset] -ge $threshold) {
    Add-IfExists -path $preset -set $set -ignorePatterns $ignorePatterns -skipMd3Patterns $skipMd3Patterns -skipMd3Presets $SkipMd3Presets
    }
  }
}

function Update-PresetBlacklist([string]$blacklistPath, [string]$managedBlacklistPath, [System.Collections.Generic.HashSet[string]]$autoSet) {
  function Set-TextFileAtomically([string]$path, [string[]]$value) {
    $directory = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $directory)) {
      [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    }

    $tempPath = Join-Path $directory ([System.IO.Path]::GetFileName($path) + '.' + [System.Guid]::NewGuid().ToString('N') + '.tmp')
    $replaceBackupPath = Join-Path $directory ([System.IO.Path]::GetFileName($path) + '.' + [System.Guid]::NewGuid().ToString('N') + '.replace-bak')
    $encoding = New-Object System.Text.UTF8Encoding $false
    try {
      [System.IO.File]::WriteAllLines($tempPath, [string[]]$value, $encoding)
      if (Test-Path -LiteralPath $path) {
        [System.IO.File]::Replace($tempPath, $path, $replaceBackupPath)
        if (Test-Path -LiteralPath $replaceBackupPath) {
          Remove-Item -LiteralPath $replaceBackupPath -Force
        }
      }
      else {
        [System.IO.File]::Move($tempPath, $path)
      }
    }
    finally {
      if (Test-Path -LiteralPath $tempPath) {
        Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
      }
      if (Test-Path -LiteralPath $replaceBackupPath) {
        Remove-Item -LiteralPath $replaceBackupPath -Force -ErrorAction SilentlyContinue
      }
    }
  }

  function New-StringSet() {
    return ,(New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase))
  }

  function Read-EntrySet([string]$path) {
    $set = New-StringSet
    if (Test-Path $path) {
      foreach ($line in Get-Content -Path $path) {
        $item = $line.Trim()
        if ($item) {
          [void]$set.Add($item)
        }
      }
    }
    return ,$set
  }

  function Get-SortedEntries([System.Collections.Generic.HashSet[string]]$set) {
    $set | Where-Object { $_ } | Sort-Object -Unique
  }

  $previousSet = Read-EntrySet -path $blacklistPath
  $managedSet = Read-EntrySet -path $managedBlacklistPath
  $autoSorted = @(Get-SortedEntries -set $autoSet)
  $previous = @(Get-SortedEntries -set $previousSet)
  $alreadyPresent = @($autoSorted | Where-Object { $previousSet.Contains($_) })
  $wouldAdd = @($autoSorted | Where-Object { -not $previousSet.Contains($_) })

  $staleManaged = @()
  if ($PruneStaleBlacklist) {
    $staleManaged = @($managedSet | Where-Object { $previousSet.Contains($_) -and -not $autoSet.Contains($_) } | Sort-Object -Unique)
  }

  $staleSet = New-StringSet
  foreach ($item in $staleManaged) {
    [void]$staleSet.Add($item)
  }

  $finalSet = New-StringSet
  foreach ($item in $previous) {
    if ($PruneStaleBlacklist -and $staleSet.Contains($item)) {
      continue
    }
    [void]$finalSet.Add($item)
  }
  foreach ($item in $autoSorted) {
    [void]$finalSet.Add($item)
  }
  $final = @(Get-SortedEntries -set $finalSet)

  if ($DryRun) {
    Write-Host ('Dry-run: blacklist target = ' + $blacklistPath)
    Write-Host ('Existing entries: ' + $previous.Count)
    Write-Host ('Detected this run: ' + $autoSorted.Count)
    Write-Host ('Already in blacklist: ' + $alreadyPresent.Count)

    if ($wouldAdd.Count -eq 0) {
      Write-Host 'Dry-run: no new entries to add.'
    }
    else {
      Write-Host ('Dry-run: would add ' + $wouldAdd.Count + ' new entries.')
      if ($FullDryRun) {
        Write-Host 'Dry-run full list:'
        $wouldAdd | ForEach-Object { Write-Host ('  ' + $_) }
      }
      else {
        Write-Host ('Dry-run preview (first ' + $DryRunLimit + '):')
        if ($DryRunLimit -gt 0) {
          $wouldAdd | Select-Object -First $DryRunLimit | ForEach-Object { Write-Host ('  ' + $_) }
          if ($wouldAdd.Count -gt $DryRunLimit) {
            Write-Host ('  ... and ' + ($wouldAdd.Count - $DryRunLimit) + ' more.')
          }
        }
      }
    }

    if ($staleManaged.Count -eq 0) {
      Write-Host 'Dry-run: no stale scanner-managed entries to remove.'
    }
    else {
      Write-Host ('Dry-run: would remove ' + $staleManaged.Count + ' stale scanner-managed entries.')
      if ($FullDryRun) {
        Write-Host 'Dry-run stale removal list:'
        $staleManaged | ForEach-Object { Write-Host ('  ' + $_) }
      }
      else {
        Write-Host ('Dry-run stale removal preview (first ' + $DryRunLimit + '):')
        if ($DryRunLimit -gt 0) {
          $staleManaged | Select-Object -First $DryRunLimit | ForEach-Object { Write-Host ('  ' + $_) }
          if ($staleManaged.Count -gt $DryRunLimit) {
            Write-Host ('  ... and ' + ($staleManaged.Count - $DryRunLimit) + ' more.')
          }
        }
      }
    }

    if ($alreadyPresent.Count -gt 0 -and $FullDryRun) {
      Write-Host ('Dry-run review (already-present): ' + $alreadyPresent.Count)
      $alreadyPresent | ForEach-Object { Write-Host ('  ' + $_) }
    }
    Write-Host ('Dry-run: total would be ' + ($final.Count) + ' entries.')
    return
  }

  $hasBlacklistChanges = ($wouldAdd.Count -gt 0) -or ($staleManaged.Count -gt 0) -or -not (Test-Path $blacklistPath)
  if ($hasBlacklistChanges -and (Test-Path $blacklistPath)) {
    $backup = Join-Path (Split-Path -Parent $blacklistPath) ('preset-blacklist.bak-' + $timestamp)
    Copy-Item -LiteralPath $blacklistPath -Destination $backup -Force
  }

  if ($hasBlacklistChanges) {
    Set-TextFileAtomically -path $blacklistPath -value $final
    Write-Host ("Updated preset-blacklist with $($final.Count) total entries (new=$($wouldAdd.Count), removed=$($staleManaged.Count)).")
  }
  else {
    Write-Host "No blacklist changes required."
  }

  Set-TextFileAtomically -path $managedBlacklistPath -value $autoSorted
  Write-Host "Scanner-managed blacklist set refreshed with $($autoSorted.Count) entries."
}

$autoBlacklist = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
Import-PresetScanCache -path $scanCachePath -set $autoBlacklist -ignorePatterns $ignorePatterns

if (-not (Test-Path $resetSafetyCacheMarker)) {
  Import-ShaderFallbackCache -path $logPath -threshold $BadPresetStrikeThreshold -set $autoBlacklist -ignorePatterns $ignorePatterns
  Import-ShaderDebugFailures -path $shaderLogPath -threshold $ShaderErrorMinCount -lookbackDays 14 -set $autoBlacklist -ignorePatterns $ignorePatterns
}

Update-PresetBlacklist -blacklistPath $blacklistPath -managedBlacklistPath $managedBlacklistPath -autoSet $autoBlacklist

$selectedProfile = $Profile
$reason = 'explicit'

if ($Profile -eq 'auto') {
  $selectedProfile = 'md3'
  $reason = 'no recent crash markers detected'

  $startWindow = (Get-Date).AddSeconds(-[math]::Abs([double]$CrashWindowSeconds))
  $crashDir = Join-Path $root 'crashlogs'
  $recentCrash = Get-ChildItem -Path $crashDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $startWindow }

  if ($recentCrash.Count -gt 0) {
    $selectedProfile = $FallbackProfile
    $reason = 'recent crash marker detected, using fallback'
  }
}

if ($DryRun) {
  Write-Host "Dry-run profile decision: $selectedProfile ($reason)"
}

$src = Join-Path $root ("milk2_{0}.ini" -f $selectedProfile)
$dst = Join-Path $root 'milk2.ini'
if ($DryRun) {
  Write-Host "Dry-run: profile file writes skipped."
  if (-not(Test-Path $src)) {
    Write-Warning "Profile file not found (dry-run): $src"
  }
  else {
    Write-Host ('Dry-run: profile source = ' + $src)
    Write-Host ('Dry-run: profile target = ' + $dst)
  }
}
elseif ($NoProfileSwitch) {
  Write-Host "NoProfileSwitch enabled: skipping profile copy/log updates."
}
else {
  if (-not(Test-Path $src)) {
    throw "Profile file not found: $src"
  }
  Copy-Item -LiteralPath $src -Destination $dst -Force
  Set-Content -Path $modePath -Value $selectedProfile -Encoding UTF8
  $logLine = ('{0:u} profile={1} reason={2} source={3} fallback={4} crashWindow={5}' -f (Get-Date), $selectedProfile, $reason, $Profile, $FallbackProfile, $CrashWindowSeconds)
  Add-Content -Path $logPath -Value $logLine
  Write-Host "Switched MilkDrop profile to: $selectedProfile ($reason)"
}

