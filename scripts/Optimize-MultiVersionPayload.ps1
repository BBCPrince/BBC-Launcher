param(
    [Parameter(Mandatory = $true)]
    [string]$InputRoot,
    [string]$OutputRoot
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "local-test-files\LocalState-MultiVersion-Deduped"
}

$InputRoot = [System.IO.Path]::GetFullPath($InputRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$localTestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local-test-files"))
$targetWithSlash = $OutputRoot.TrimEnd('\') + '\'
$allowedRootWithSlash = $localTestRoot.TrimEnd('\') + '\'
if (-not $targetWithSlash.StartsWith($allowedRootWithSlash, [System.StringComparison]::OrdinalIgnoreCase) -or
    $targetWithSlash -eq $allowedRootWithSlash) {
    throw "Refusing to recreate output root outside local-test-files: $OutputRoot"
}

if (-not (Test-Path (Join-Path $InputRoot "profiles"))) {
    throw "Input root is missing profiles\: $InputRoot"
}

if (Test-Path $OutputRoot) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Invoke-RobocopyChecked {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExtraArgs = @()
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & robocopy $Source $Destination /E /R:2 /W:1 /NFL /NDL /NP @ExtraArgs | Out-Host
    if ($LASTEXITCODE -gt 7) {
        throw "robocopy failed with exit code $LASTEXITCODE copying $Source to $Destination"
    }
}

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Text
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-RelativePath {
    param(
        [string]$Root,
        [string]$Path
    )

    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is not under root. Root=$fullRoot Path=$fullPath"
    }

    return $fullPath.Substring($fullRoot.Length).Replace('/', '\')
}

function Get-DirectorySignature {
    param([string]$Root)

    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object FullName) {
        $relative = Get-RelativePath -Root $Root -Path $file.FullName
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
        $parts.Add("$relative|$($file.Length)|$hash")
    }

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($parts -join "`n")
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "")
    }
    finally {
        $sha.Dispose()
    }
}

function Get-FileHashFast {
    param([string]$Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}

function Read-ClasspathLines {
    param([string]$ProfileRoot)

    $path = Join-Path $ProfileRoot "minecraft-classpath.txt"
    if (-not (Test-Path $path)) {
        return @()
    }

    return @(Get-Content -LiteralPath $path | ForEach-Object { [string]$_ })
}

function Write-ClasspathLines {
    param(
        [string]$ProfileRoot,
        [string[]]$Lines
    )

    Write-Utf8NoBom -Path (Join-Path $ProfileRoot "minecraft-classpath.txt") -Text (($Lines -join "`r`n") + "`r`n")
}

function Read-ModulePathRelativeLibraries {
    param([string]$ProfileRoot)

    $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $path = Join-Path $ProfileRoot "minecraft-jvm-args.txt"
    if (-not (Test-Path $path)) {
        return $set
    }

    $args = @(Get-Content -LiteralPath $path)
    for ($i = 0; $i -lt $args.Count; $i++) {
        $arg = [string]$args[$i]
        if ($arg -ne "-p" -and $arg -ne "--module-path") {
            continue
        }
        if (($i + 1) -ge $args.Count) {
            continue
        }

        foreach ($entry in ([string]$args[$i + 1] -split "\$\{classpath_separator\}")) {
            $relative = $entry.Trim()
            if (-not $relative) {
                continue
            }

            $relative = $relative.Replace('${library_directory}/', 'libraries/')
            $relative = $relative.Replace('${library_directory}\', 'libraries\')
            $relative = $relative.Replace('/', '\')
            if ($relative.StartsWith("libraries\", [System.StringComparison]::OrdinalIgnoreCase) -and
                $relative.EndsWith(".jar", [System.StringComparison]::OrdinalIgnoreCase)) {
                [void]$set.Add($relative)
            }
        }
    }

    return ,$set
}

function Remove-ManifestEntries {
    param(
        [string]$ProfileRoot,
        [System.Collections.Generic.HashSet[string]]$RelativePaths
    )

    if ($RelativePaths.Count -eq 0) {
        return 0
    }

    $path = Join-Path $ProfileRoot "download-manifest.json"
    if (-not (Test-Path $path)) {
        return 0
    }

    $json = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if (-not $json.Entries) {
        return 0
    }

    $before = @($json.Entries).Count
    $json.Entries = @($json.Entries | Where-Object {
        $relative = ([string]$_.LocalRelativePath).Replace('/', '\')
        -not $RelativePaths.Contains($relative)
    })
    $after = @($json.Entries).Count
    if ($after -ne $before) {
        Write-Utf8NoBom -Path $path -Text (($json | ConvertTo-Json -Depth 12) + "`r`n")
    }

    return ($before - $after)
}

function Remove-EmptyDirectories {
    param([string]$Root)

    if (-not (Test-Path $Root)) {
        return
    }

    Get-ChildItem -LiteralPath $Root -Recurse -Directory |
        Sort-Object FullName -Descending |
        ForEach-Object {
            if (-not (Get-ChildItem -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue)) {
                Remove-Item -LiteralPath $_.FullName -Force
            }
        }
}

Write-Host "Copying payload to optimization output..."
Invoke-RobocopyChecked -Source $InputRoot -Destination $OutputRoot -ExtraArgs @("/XD", (Join-Path $InputRoot "_downloads"))

$profilesRoot = Join-Path $OutputRoot "profiles"
$profileDirs = @(Get-ChildItem -LiteralPath $profilesRoot -Directory | Sort-Object Name)
if ($profileDirs.Count -eq 0) {
    throw "No profiles found under $profilesRoot"
}

$savedBytes = 0L
$removedManifestEntries = 0
$sharedRuntimeRecords = New-Object System.Collections.Generic.List[object]

Write-Host "Finding identical Java runtimes..."
$runtimeGroups = @{}
foreach ($profile in $profileDirs) {
    $runtimeRoot = Join-Path $profile.FullName "runtime\jre"
    if (-not (Test-Path $runtimeRoot)) {
        continue
    }

    $signature = Get-DirectorySignature -Root $runtimeRoot
    if (-not $runtimeGroups.ContainsKey($signature)) {
        $runtimeGroups[$signature] = @()
    }
    $runtimeGroups[$signature] = @($runtimeGroups[$signature]) + [pscustomobject]@{
        Profile = $profile
        RuntimeRoot = $runtimeRoot
        Size = (Get-ChildItem -LiteralPath $runtimeRoot -Recurse -File | Measure-Object Length -Sum).Sum
    }
}

$runtimeIndex = 0
foreach ($key in @($runtimeGroups.Keys)) {
    $group = @($runtimeGroups[$key])
    if ($group.Count -lt 2) {
        continue
    }

    $runtimeIndex++
    $sharedRelative = "shared\runtimes\runtime-$runtimeIndex\jre"
    $sharedRoot = Join-Path $OutputRoot $sharedRelative
    Invoke-RobocopyChecked -Source $group[0].RuntimeRoot -Destination $sharedRoot

    foreach ($item in $group) {
        Remove-Item -LiteralPath (Join-Path $item.Profile.FullName "runtime") -Recurse -Force
        Write-Utf8NoBom -Path (Join-Path $item.Profile.FullName "shared-runtime.txt") -Text ("..\..\" + $sharedRelative + "`r`n")

        $manifestSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $runtimeRelativeRoot = "runtime\jre"
        Get-ChildItem -LiteralPath $sharedRoot -Recurse -File | ForEach-Object {
            $relative = Get-RelativePath -Root $sharedRoot -Path $_.FullName
            [void]$manifestSet.Add($runtimeRelativeRoot + "\" + $relative)
        }
        $removedManifestEntries += Remove-ManifestEntries -ProfileRoot $item.Profile.FullName -RelativePaths $manifestSet
    }

    $savedBytes += [int64]$group[0].Size * ($group.Count - 1)
    $sharedRuntimeRecords.Add([ordered]@{
        SharedPath = $sharedRelative
        Profiles = @($group | ForEach-Object { $_.Profile.Name })
        SizeBytes = [int64]$group[0].Size
        SavedBytes = [int64]$group[0].Size * ($group.Count - 1)
    })
}

Write-Host "Finding duplicate classpath jars..."
$classpathRecords = New-Object System.Collections.Generic.List[object]
$profileClasspath = @{}
$modulePathByProfile = @{}
foreach ($profile in $profileDirs) {
    $profileClasspath[$profile.Name] = @(Read-ClasspathLines -ProfileRoot $profile.FullName)
    $modulePathByProfile[$profile.Name] = Read-ModulePathRelativeLibraries -ProfileRoot $profile.FullName
}

$candidateMap = @{}
foreach ($profile in $profileDirs) {
    foreach ($line in $profileClasspath[$profile.Name]) {
        $relative = $line.Trim().Replace('/', '\')
        $modulePathSet = $modulePathByProfile[$profile.Name]
        if (-not $relative -or
            $relative.Contains(":") -or
            $relative.StartsWith("..\") -or
            $relative.StartsWith("\") -or
            $relative.StartsWith("libraries\cpw\mods\", [System.StringComparison]::OrdinalIgnoreCase) -or
            ($null -ne $modulePathSet -and $modulePathSet.Contains($relative))) {
            continue
        }

        $fullPath = Join-Path $profile.FullName $relative
        if (-not (Test-Path $fullPath)) {
            continue
        }

        $hash = Get-FileHashFast -Path $fullPath
        $key = "$relative|$hash"
        if (-not $candidateMap.ContainsKey($key)) {
            $candidateMap[$key] = @()
        }
        $candidateMap[$key] = @($candidateMap[$key]) + [pscustomobject]@{
            Profile = $profile
            Relative = $relative
            FullPath = $fullPath
            Size = (Get-Item -LiteralPath $fullPath).Length
            Hash = $hash
        }
    }
}

$classpathUpdates = @{}
$manifestRemovalsByProfile = @{}
foreach ($profile in $profileDirs) {
    $classpathUpdates[$profile.Name] = @{}
    $manifestRemovalsByProfile[$profile.Name] = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
}

foreach ($key in @($candidateMap.Keys)) {
    $group = @($candidateMap[$key])
    $uniqueProfiles = @($group | Group-Object { $_.Profile.Name })
    if ($uniqueProfiles.Count -lt 2) {
        continue
    }

    $first = $group[0]
    $sharedRelative = "shared\classpath\" + $first.Relative
    $sharedFullPath = Join-Path $OutputRoot $sharedRelative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sharedFullPath) | Out-Null
    Copy-Item -LiteralPath $first.FullPath -Destination $sharedFullPath -Force

    foreach ($item in $group) {
        Remove-Item -LiteralPath $item.FullPath -Force
        $classpathUpdates[$item.Profile.Name][$item.Relative] = "..\..\" + $sharedRelative
        [void]$manifestRemovalsByProfile[$item.Profile.Name].Add($item.Relative)
    }

    $savedBytes += [int64]$first.Size * ($uniqueProfiles.Count - 1)
    $classpathRecords.Add([ordered]@{
        RelativePath = $first.Relative
        SharedPath = $sharedRelative
        Profiles = @($uniqueProfiles | ForEach-Object { $_.Name })
        SizeBytes = [int64]$first.Size
        SavedBytes = [int64]$first.Size * ($uniqueProfiles.Count - 1)
    })
}

foreach ($profile in $profileDirs) {
    $updates = $classpathUpdates[$profile.Name]
    if ($updates.Count -gt 0) {
        $newLines = @($profileClasspath[$profile.Name] | ForEach-Object {
            $relative = ([string]$_).Trim().Replace('/', '\')
            if ($updates.ContainsKey($relative)) {
                $updates[$relative]
            }
            else {
                [string]$_
            }
        })
        Write-ClasspathLines -ProfileRoot $profile.FullName -Lines $newLines
    }

    $removedManifestEntries += Remove-ManifestEntries -ProfileRoot $profile.FullName -RelativePaths $manifestRemovalsByProfile[$profile.Name]
    Remove-EmptyDirectories -Root $profile.FullName
}

$summaryPath = Join-Path $OutputRoot "multi-staging-summary.json"
if (Test-Path $summaryPath) {
    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
}
else {
    $summary = [pscustomobject]@{
        Format = "MinecraftXboxMultiVersionPayload"
        Profiles = @()
    }
}

$summary | Add-Member -NotePropertyName Optimization -NotePropertyValue ([ordered]@{
    Format = "MinecraftXboxDedupedPayload"
    CreatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    InputRoot = $InputRoot
    SharedRuntimes = $sharedRuntimeRecords
    SharedClasspathFiles = $classpathRecords.Count
    RemovedManifestEntries = $removedManifestEntries
    EstimatedSavedBytes = $savedBytes
}) -Force
Write-Utf8NoBom -Path $summaryPath -Text (($summary | ConvertTo-Json -Depth 12) + "`r`n")

$inputSize = (Get-ChildItem -LiteralPath $InputRoot -Recurse -File | Measure-Object Length -Sum).Sum
$outputSize = (Get-ChildItem -LiteralPath $OutputRoot -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "Optimized multi-version payload:"
Write-Host "  $OutputRoot"
Write-Host "Input size MB:  $([math]::Round($inputSize / 1MB, 2))"
Write-Host "Output size MB: $([math]::Round($outputSize / 1MB, 2))"
Write-Host "Saved MB:       $([math]::Round(($inputSize - $outputSize) / 1MB, 2))"
Write-Host "Shared runtimes: $($sharedRuntimeRecords.Count)"
Write-Host "Shared classpath files: $($classpathRecords.Count)"
Write-Host "Removed manifest entries: $removedManifestEntries"
