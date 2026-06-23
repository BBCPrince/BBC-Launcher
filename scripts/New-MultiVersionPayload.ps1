param(
    [string]$OutputRoot,
    [string[]]$ProfileRoots,
    [switch]$SkipSharedAssets,
    [switch]$EnableAccountSignin
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "local-test-files\LocalState-MultiVersion"
}
if (-not $ProfileRoots -or $ProfileRoots.Count -eq 0) {
    $fabric1211 = Join-Path $repoRoot "local-test-files\LocalState-1.21.1-Fabric"
    $profile1211 = if (Test-Path $fabric1211) {
        $fabric1211
    }
    else {
        Join-Path $repoRoot "local-test-files\LocalState"
    }
    $fabric12111 = Join-Path $repoRoot "local-test-files\LocalState-1.21.11-Fabric"
    $profile12111 = if (Test-Path $fabric12111) {
        $fabric12111
    }
    else {
        Join-Path $repoRoot "local-test-files\LocalState-1.21.11"
    }
    $defaultProfileRoots = New-Object System.Collections.Generic.List[string]
    $defaultProfileRoots.Add($profile1211)
    $neoForge1211 = Join-Path $repoRoot "local-test-files\LocalState-1.21.1-NeoForge"
    if (Test-Path $neoForge1211) {
        $defaultProfileRoots.Add($neoForge1211)
    }

    $fabric1201 = Join-Path $repoRoot "local-test-files\LocalState-1.20.1-Fabric"
    if (Test-Path $fabric1201) {
        $defaultProfileRoots.Add($fabric1201)
    }

    $forge1201 = Join-Path $repoRoot "local-test-files\LocalState-1.20.1-Forge"
    $neoForge1201 = Join-Path $repoRoot "local-test-files\LocalState-1.20.1-NeoForge"
    if (Test-Path $forge1201) {
        $defaultProfileRoots.Add($forge1201)
    }
    elseif (Test-Path $neoForge1201) {
        $defaultProfileRoots.Add($neoForge1201)
    }

    $defaultProfileRoots.Add($profile12111)

    $forge1122 = Join-Path $repoRoot "local-test-files\LocalState-1.12.2-Forge"
    if (Test-Path $forge1122) {
        $defaultProfileRoots.Add($forge1122)
    }
    else {
        $defaultProfileRoots.Add((Join-Path $repoRoot "local-test-files\LocalState-1.12.2"))
    }

    $ProfileRoots = @($defaultProfileRoots)
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$localTestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local-test-files"))
$targetWithSlash = $OutputRoot.TrimEnd('\') + '\'
$allowedRootWithSlash = $localTestRoot.TrimEnd('\') + '\'
if (-not $targetWithSlash.StartsWith($allowedRootWithSlash, [System.StringComparison]::OrdinalIgnoreCase) -or
    $targetWithSlash -eq $allowedRootWithSlash) {
    throw "Refusing to recreate output root outside local-test-files: $OutputRoot"
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
    & robocopy $Source $Destination /E /R:2 /W:1 /NFL /NDL /NP @ExtraArgs | Out-Null
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
        [string]$Base,
        [string]$Path
    )

    $baseFull = [System.IO.Path]::GetFullPath($Base).TrimEnd('\') + '\'
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    $baseUri = New-Object System.Uri($baseFull)
    $pathUri = New-Object System.Uri($pathFull)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function Get-ProfileModulePathLibraries {
    param(
        [string]$ProfileRoot
    )

    $libraries = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $argsPath = Join-Path $ProfileRoot "minecraft-jvm-args.txt"
    if (-not (Test-Path $argsPath)) {
        return ,$libraries
    }

    $args = @(Get-Content $argsPath)
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
                [void]$libraries.Add($relative)
            }
        }
    }

    return ,$libraries
}

function Repair-NeoForgeJvmArgsForNativeLaunch {
    param(
        [string]$ProfileRoot
    )

    $argsPath = Join-Path $ProfileRoot "minecraft-jvm-args.txt"
    if (-not (Test-Path $argsPath)) {
        return
    }

    $args = New-Object System.Collections.Generic.List[string]
    foreach ($line in @(Get-Content $argsPath)) {
        $args.Add([string]$line)
    }

    $changed = $false
    for ($i = 0; $i -lt $args.Count; $i++) {
        $arg = [string]$args[$i]
        if (($arg -ne "-p" -and $arg -ne "--module-path") -or ($i + 1) -ge $args.Count) {
            continue
        }

        $oldModulePath = [string]$args[$i + 1]
        $entries = @($oldModulePath -split "\$\{classpath_separator\}" | Where-Object {
            $_ -notmatch "(^|/|\\)cpw[/\\]mods[/\\]bootstraplauncher[/\\]"
        })
        $newModulePath = $entries -join '${classpath_separator}'
        if ($newModulePath -ne $oldModulePath) {
            $args[$i + 1] = $newModulePath
            $changed = $true
        }
    }

    if ($changed) {
        Write-Utf8NoBom -Path $argsPath -Text (($args -join "`r`n") + "`r`n")
    }
}

function Test-NeoForgeGeneratedClientJar {
    param([string]$RelativePath)

    $normalized = $RelativePath.Replace("/", "\")
    return $normalized -match '^libraries\\net\\neoforged\\(neoforge|forge)\\[^\\]+\\(neoforge|forge)-[^\\]+-client\.jar$'
}

function Test-NeoForgeUniversalJar {
    param([string]$RelativePath)

    $normalized = $RelativePath.Replace("/", "\")
    return $normalized -match '^libraries\\net\\neoforged\\(neoforge|forge)\\[^\\]+\\(neoforge|forge)-[^\\]+-universal\.jar$'
}

function Test-NeoForgeProfileRoot {
    param([string]$ProfileRoot)

    $summaryPath = Join-Path $ProfileRoot "staging-summary.json"
    if (-not (Test-Path $summaryPath)) {
        return $false
    }

    $summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
    $modLoader = [string]$summary.ModLoader
    $mainClass = [string]$summary.MainClass
    $artifactId = [string]$summary.NeoForgeArtifactId
    if ($modLoader.IndexOf("neoforge", [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        return $true
    }

    return $modLoader.IndexOf("forge", [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -and (
        $mainClass.IndexOf("bootstraplauncher", [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
        $artifactId.IndexOf("forge", [System.StringComparison]::OrdinalIgnoreCase) -ge 0
    )
}

function New-ProfileClasspath {
    param(
        [string]$ProfileRoot
    )

    $manifestPath = Join-Path $ProfileRoot "download-manifest.json"
    $entries = New-Object System.Collections.Generic.List[string]
    $modulePathLibraries = Get-ProfileModulePathLibraries -ProfileRoot $ProfileRoot
    $isNeoForgeProfile = Test-NeoForgeProfileRoot -ProfileRoot $ProfileRoot
    if (-not $isNeoForgeProfile) {
        $entries.Add("client.jar")
    }

    if (Test-Path $manifestPath) {
        $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
        foreach ($entry in @($manifest.Entries)) {
            $relative = [string]$entry.LocalRelativePath
            if (-not $relative) {
                continue
            }
            if (-not $relative.EndsWith(".jar", [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            $fileName = [System.IO.Path]::GetFileName($relative)
            if ($fileName.IndexOf("-natives-", [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                continue
            }
            if ($relative -eq "client.jar") {
                continue
            }
            if (Test-NeoForgeGeneratedClientJar -RelativePath $relative) {
                continue
            }
            if (Test-NeoForgeUniversalJar -RelativePath $relative) {
                continue
            }
            if ($modulePathLibraries.Contains($relative)) {
                continue
            }
            if (-not $entries.Contains($relative)) {
                $entries.Add($relative)
            }
        }
    }
    else {
        Get-ChildItem -Path (Join-Path $ProfileRoot "libraries") -Recurse -Filter *.jar -ErrorAction SilentlyContinue |
            Where-Object { $_.Name.IndexOf("-natives-", [System.StringComparison]::OrdinalIgnoreCase) -lt 0 } |
            ForEach-Object {
                $relative = [System.IO.Path]::GetRelativePath($ProfileRoot, $_.FullName)
                if ($modulePathLibraries.Contains($relative)) {
                    return
                }
                if (Test-NeoForgeGeneratedClientJar -RelativePath $relative) {
                    return
                }
                if (Test-NeoForgeUniversalJar -RelativePath $relative) {
                    return
                }
                if (-not $entries.Contains($relative)) {
                    $entries.Add($relative)
                }
            }
    }

    Write-Utf8NoBom -Path (Join-Path $ProfileRoot "minecraft-classpath.txt") -Text (($entries -join "`r`n") + "`r`n")
    return $entries.Count
}

$profiles = New-Object System.Collections.Generic.List[object]
foreach ($root in $ProfileRoots) {
    $fullRoot = [System.IO.Path]::GetFullPath($root)
    $summaryPath = Join-Path $fullRoot "staging-summary.json"
    if (-not (Test-Path (Join-Path $fullRoot "client.jar")) -or -not (Test-Path $summaryPath)) {
        throw "Profile root is missing client.jar or staging-summary.json: $fullRoot"
    }

    $summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
    $version = [string]$summary.Version
    if (-not $version) {
        throw "Profile root has no Version in staging-summary.json: $fullRoot"
    }

    $profileDestination = Join-Path $OutputRoot (Join-Path "profiles" $version)
    $excludedProfileDirs = @(
        "_downloads",
        "assets",
        "native",
        "game",
        "logs",
        "mods",
        "mods-library"
    ) | ForEach-Object { Join-Path $fullRoot $_ }
    $excludeProfileArgs = @("/XD") + $excludedProfileDirs
    Invoke-RobocopyChecked -Source $fullRoot -Destination $profileDestination -ExtraArgs $excludeProfileArgs
    $nativeSource = Join-Path $fullRoot "native"
    if (Test-Path $nativeSource) {
        Invoke-RobocopyChecked -Source $nativeSource -Destination (Join-Path $profileDestination "native") -ExtraArgs @(
            "/XD", (Join-Path $nativeSource "mesa-uwp"), (Join-Path $nativeSource "glon12"),
            "/XF", "xbox-glfw.dll", "xbox-opengl.dll", "xbox-openal.dll", "graphics_bridge.dll"
        )
    }
    $classpathCount = New-ProfileClasspath -ProfileRoot $profileDestination

    $assetSource = Join-Path $fullRoot "assets"
    if (-not $SkipSharedAssets -and (Test-Path $assetSource)) {
        Invoke-RobocopyChecked -Source $assetSource -Destination (Join-Path $OutputRoot "assets")
    }

    $remapCacheSource = Join-Path $fullRoot (Join-Path "game" (Join-Path $version ".fabric\remappedJars"))
    $remapCacheCopied = $false
    if (Test-Path $remapCacheSource) {
        $remapCacheDestination = Join-Path $OutputRoot (Join-Path "game" (Join-Path $version ".fabric\remappedJars"))
        Invoke-RobocopyChecked -Source $remapCacheSource -Destination $remapCacheDestination
        $remapCacheCopied = $true
    }

    $profiles.Add([ordered]@{
        Version = $version
        Source = $fullRoot
        ProfilePath = "profiles\$version"
        JavaRuntimeMajor = $summary.JavaRuntimeMajor
        AssetIndex = $summary.AssetIndex
        ClasspathEntries = $classpathCount
        RemapCache = $remapCacheCopied
    })
}

New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot "mods-library\common") | Out-Null
Write-Utf8NoBom `
    -Path (Join-Path $OutputRoot "mods-library\common\README.txt") `
    -Text "Put mod .jar files here to offer them for every profile. A Fabric, Forge, or Quilt profile is required for mods to load.`r`n"
foreach ($profile in $profiles) {
    New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot ("mods-library\" + $profile.Version)) | Out-Null
    Write-Utf8NoBom `
        -Path (Join-Path $OutputRoot ("mods-library\" + $profile.Version + "\README.txt")) `
        -Text "Put mod .jar files here to offer them for Minecraft $($profile.Version).`r`n"
}

$bundledMods = New-Object System.Collections.Generic.List[object]
$bundledModsRoot = Join-Path $repoRoot "local-test-files\BundledMods"
if (Test-Path $bundledModsRoot) {
    foreach ($profile in $profiles) {
        $source = Join-Path $bundledModsRoot $profile.Version
        if (-not (Test-Path $source)) {
            continue
        }

        $destination = Join-Path $OutputRoot ("mods-library\" + $profile.Version)
        Invoke-RobocopyChecked -Source $source -Destination $destination -ExtraArgs @("/XF", "README.txt")
        $files = @(Get-ChildItem -LiteralPath $source -Filter *.jar -File -Recurse -ErrorAction SilentlyContinue | Sort-Object FullName)
        $bundledMods.Add([ordered]@{
            Version = $profile.Version
            Source = $source
            Destination = "mods-library\$($profile.Version)"
            Files = @($files | ForEach-Object { Get-RelativePath -Base $source -Path $_.FullName })
        })
    }
}

if ($EnableAccountSignin) {
    Write-Utf8NoBom `
        -Path (Join-Path $OutputRoot "enable-account-signin") `
        -Text "Account sign-in is enabled for this packaged test payload.`r`n"
}

$multiSummary = [ordered]@{
    Format = "MinecraftXboxMultiVersionPayload"
    CreatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    SharedAssetsIncluded = -not $SkipSharedAssets
    AccountSigninEnabled = [bool]$EnableAccountSignin
    Profiles = $profiles
    ModLibrary = "mods-library"
    BundledMods = $bundledMods
}
Write-Utf8NoBom `
    -Path (Join-Path $OutputRoot "multi-staging-summary.json") `
    -Text (($multiSummary | ConvertTo-Json -Depth 8) + "`r`n")

Write-Host ""
Write-Host "Multi-version payload staged:"
Write-Host "  $OutputRoot"
Write-Host "Profiles:"
foreach ($profile in $profiles) {
    Write-Host "  $($profile.Version) -> $($profile.ProfilePath)"
}
