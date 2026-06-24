param(
    [string]$MinecraftVersion = "1.21.1",
    [string]$NeoForgeVersion,
    [string]$BaseProfileRoot,
    [string]$OutputRoot,
    [switch]$Clean,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $BaseProfileRoot) {
    $BaseProfileRoot = Join-Path $repoRoot "local-test-files\LocalState"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "local-test-files\LocalState-$MinecraftVersion-NeoForge"
}

$BaseProfileRoot = [System.IO.Path]::GetFullPath($BaseProfileRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$localTestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local-test-files"))
$targetWithSlash = $OutputRoot.TrimEnd('\') + '\'
$allowedRootWithSlash = $localTestRoot.TrimEnd('\') + '\'
if (-not $targetWithSlash.StartsWith($allowedRootWithSlash, [System.StringComparison]::OrdinalIgnoreCase) -or
    $targetWithSlash -eq $allowedRootWithSlash) {
    throw "Refusing to create NeoForge profile outside local-test-files: $OutputRoot"
}

if (-not (Test-Path (Join-Path $BaseProfileRoot "client.jar")) -or
    -not (Test-Path (Join-Path $BaseProfileRoot "staging-summary.json"))) {
    throw "Base profile root is missing client.jar or staging-summary.json: $BaseProfileRoot"
}

function Invoke-RobocopyChecked {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExtraArgs = @()
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & robocopy $Source $Destination /E /R:2 /W:1 @ExtraArgs | Out-Host
    if ($LASTEXITCODE -gt 7) {
        throw "robocopy failed with exit code $LASTEXITCODE copying $Source to $Destination"
    }
}

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Text
    )

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-Sha1 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA1).Hash.ToLowerInvariant()
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Save-Download {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Sha1,
        [long]$Size = 0
    )

    if ((Test-Path $Path) -and -not $Force) {
        if ($Sha1 -and ((Get-Sha1 $Path) -ne $Sha1.ToLowerInvariant())) {
            Write-Host "Hash mismatch, re-downloading $Path"
        }
        elseif ($Size -gt 0 -and ((Get-Item $Path).Length -ne $Size)) {
            Write-Host "Size mismatch, re-downloading $Path"
        }
        else {
            return
        }
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $tempPath = "$Path.download"
    if (Test-Path $tempPath) {
        Remove-Item -LiteralPath $tempPath -Force
    }

    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $tempPath

    if ($Size -gt 0 -and ((Get-Item $tempPath).Length -ne $Size)) {
        throw "Downloaded file size mismatch for $Path"
    }

    if ($Sha1 -and ((Get-Sha1 $tempPath) -ne $Sha1.ToLowerInvariant())) {
        throw "Downloaded file hash mismatch for $Path"
    }

    Move-Item -LiteralPath $tempPath -Destination $Path -Force
}

function Add-ManifestEntryUnique {
    param(
        [System.Collections.Generic.List[object]]$Entries,
        [string]$RemoteUrl,
        [string]$RelativePath,
        [long]$Size = 0
    )

    $normalized = $RelativePath.Replace("/", "\")
    foreach ($entry in @($Entries)) {
        if ([string]$entry.LocalRelativePath -eq $normalized) {
            return
        }
    }

    $fullPath = Join-Path $OutputRoot $normalized
    if (-not (Test-Path $fullPath)) {
        throw "Manifest entry file missing: $fullPath"
    }

    $Entries.Add([ordered]@{
        RemoteUrl = $RemoteUrl
        LocalRelativePath = $normalized
        ExpectedSizeBytes = if ($Size -gt 0) { $Size } else { (Get-Item $fullPath).Length }
        Sha256 = Get-Sha256 $fullPath
        RequiredBeforeLaunch = $true
    })
}

function Get-NeoForgeArtifactInfo {
    param([string]$MinecraftVersion)

    if ($MinecraftVersion -eq "1.21.1") {
        return [pscustomobject]@{
            ArtifactId = "neoforge"
            VersionPrefix = "21.1."
            VersionPattern = '^\d+\.\d+\.\d+$'
        }
    }

    if ($MinecraftVersion -eq "1.20.1") {
        return [pscustomobject]@{
            ArtifactId = "forge"
            VersionPrefix = "1.20.1-47.1."
            VersionPattern = '^1\.20\.1-47\.1\.\d+$'
        }
    }

    throw "NeoForge artifact layout is not known for Minecraft $MinecraftVersion"
}

function Get-LatestNeoForgeVersion {
    param([string]$MinecraftVersion)

    $info = Get-NeoForgeArtifactInfo -MinecraftVersion $MinecraftVersion
    $prefix = [string]$info.VersionPrefix
    $metadataUrl = "https://maven.neoforged.net/releases/net/neoforged/$($info.ArtifactId)/maven-metadata.xml"
    $metadata = [xml](Invoke-WebRequest -Uri $metadataUrl -UseBasicParsing).Content
    $matches = @($metadata.metadata.versioning.versions.version | Where-Object {
        $_.StartsWith($prefix) -and ($_ -match [string]$info.VersionPattern)
    })
    if ($matches.Count -eq 0) {
        throw "No NeoForge versions found for Minecraft $MinecraftVersion"
    }

    return @($matches | Sort-Object { [int]($_.Substring($prefix.Length)) } | Select-Object -Last 1)[0]
}

$neoForgeInfo = Get-NeoForgeArtifactInfo -MinecraftVersion $MinecraftVersion
$neoForgeArtifactId = [string]$neoForgeInfo.ArtifactId
$loaderSlug = if ($neoForgeArtifactId -eq "forge") { "forge" } else { "neoforge" }
$loaderName = if ($neoForgeArtifactId -eq "forge") { "Forge" } else { "NeoForge" }

if (-not $NeoForgeVersion) {
    $NeoForgeVersion = Get-LatestNeoForgeVersion -MinecraftVersion $MinecraftVersion
}
elseif ($NeoForgeVersion.StartsWith("neoforge-")) {
    $NeoForgeVersion = $NeoForgeVersion.Substring("neoforge-".Length)
}
elseif ($NeoForgeVersion.StartsWith("forge-")) {
    $NeoForgeVersion = $NeoForgeVersion.Substring("forge-".Length)
}

if ($neoForgeArtifactId -eq "forge" -and $NeoForgeVersion.StartsWith("$MinecraftVersion-")) {
    $neoForgeLauncherVersion = "$MinecraftVersion-forge-" + $NeoForgeVersion.Substring($MinecraftVersion.Length + 1)
}
else {
    $neoForgeLauncherVersion = "neoforge-$NeoForgeVersion"
}
$profileId = "$MinecraftVersion-$loaderSlug-$NeoForgeVersion"

if ($Clean -and (Test-Path $OutputRoot)) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}

Invoke-RobocopyChecked -Source $BaseProfileRoot -Destination $OutputRoot -ExtraArgs @(
    "/XD", "_downloads", "game", "logs", "mods", "mods-library"
)

$downloadRoot = Join-Path $OutputRoot "_downloads\$loaderSlug"
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
$installerUrl = "https://maven.neoforged.net/releases/net/neoforged/$neoForgeArtifactId/$NeoForgeVersion/$neoForgeArtifactId-$NeoForgeVersion-installer.jar"
$installerPath = Join-Path $downloadRoot "$neoForgeArtifactId-$NeoForgeVersion-installer.jar"
Save-Download -Url $installerUrl -Path $installerPath

$launcherProfilesPath = Join-Path $OutputRoot "launcher_profiles.json"
Write-Utf8NoBom -Path $launcherProfilesPath -Text '{"profiles":{},"settings":{},"version":3}'

$javaPath = Join-Path $OutputRoot "runtime\jre\bin\java.exe"
if (-not (Test-Path $javaPath)) {
    throw "Java runtime missing from $loaderName profile root: $javaPath"
}

& $javaPath -jar $installerPath --installClient $OutputRoot
if ($LASTEXITCODE -ne 0) {
    throw "$loaderName installer failed with exit code $LASTEXITCODE"
}

if (Test-Path $launcherProfilesPath) {
    Remove-Item -LiteralPath $launcherProfilesPath -Force
}

$neoForgeVersionDir = Join-Path $OutputRoot "versions\$neoForgeLauncherVersion"
$neoForgeVersionJsonPath = Join-Path $neoForgeVersionDir "$neoForgeLauncherVersion.json"
if (-not (Test-Path $neoForgeVersionJsonPath)) {
    $versionsRoot = Join-Path $OutputRoot "versions"
    $candidate = Get-ChildItem -Path $versionsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name.IndexOf($NeoForgeVersion, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $_.Name.IndexOf("neoforge", [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $_.Name.IndexOf($neoForgeArtifactId, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
        } |
        Where-Object { Test-Path (Join-Path $_.FullName "$($_.Name).json") } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if (-not $candidate) {
        throw "$loaderName version json missing after install: $neoForgeVersionJsonPath"
    }

    $neoForgeLauncherVersion = $candidate.Name
    $neoForgeVersionDir = $candidate.FullName
    $neoForgeVersionJsonPath = Join-Path $neoForgeVersionDir "$neoForgeLauncherVersion.json"
}

$neoForgeVersionJsonText = Get-Content $neoForgeVersionJsonPath -Raw
$neoForgeVersionJson = $neoForgeVersionJsonText | ConvertFrom-Json

function Get-NeoForgeLocalLibraryPath {
    param(
        [string]$LibraryName,
        [string]$ArtifactPath
    )

    if ($LibraryName -eq "com.google.guava:listenablefuture:9999.0-empty-to-avoid-conflict-with-guava") {
        return "libraries\com\google\guava\listenablefuture\9999\listenablefuture.jar"
    }

    return Join-Path "libraries" ($ArtifactPath.Replace("/", "\"))
}

function Remove-BootstrapLauncherFromModulePath {
    param(
        [System.Collections.Generic.List[string]]$Args
    )

    for ($i = 0; $i -lt $Args.Count; $i++) {
        $arg = [string]$Args[$i]
        if (($arg -ne "-p" -and $arg -ne "--module-path") -or ($i + 1) -ge $Args.Count) {
            continue
        }

        $entries = @([string]$Args[$i + 1] -split "\$\{classpath_separator\}" | Where-Object {
            $_ -notmatch "(^|/|\\)cpw[/\\]mods[/\\]bootstraplauncher[/\\]"
        })
        $Args[$i + 1] = $entries -join '${classpath_separator}'
    }
}

function Add-JvmIgnoreListEntry {
    param(
        [System.Collections.Generic.List[string]]$Args,
        [string]$EntryName
    )

    if ([string]::IsNullOrWhiteSpace($EntryName)) {
        return
    }

    for ($i = 0; $i -lt $Args.Count; $i++) {
        $arg = [string]$Args[$i]
        if (-not $arg.StartsWith("-DignoreList=", [System.StringComparison]::Ordinal)) {
            continue
        }

        if ($arg.IndexOf($EntryName, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
            $Args[$i] = "$arg,$EntryName"
        }
        return
    }
}

$jvmArgs = New-Object System.Collections.Generic.List[string]
foreach ($arg in @($neoForgeVersionJson.arguments.jvm)) {
    if ($arg -is [string]) {
        $jvmArgs.Add($arg)
    }
}

$clientLibraryRoot = Join-Path $OutputRoot "libraries\net\minecraft\client"
if (Test-Path $clientLibraryRoot) {
    Get-ChildItem -Path $clientLibraryRoot -Recurse -Filter "*-srg.jar" -ErrorAction SilentlyContinue |
        ForEach-Object {
            Add-JvmIgnoreListEntry -Args $jvmArgs -EntryName $_.Name
        }
}
Write-Utf8NoBom -Path (Join-Path $OutputRoot "minecraft-jvm-args.txt") -Text (($jvmArgs -join "`r`n") + "`r`n")

$gameArgs = New-Object System.Collections.Generic.List[string]
foreach ($arg in @($neoForgeVersionJson.arguments.game)) {
    if ($arg -is [string]) {
        $gameArgs.Add($arg)
    }
}
Write-Utf8NoBom -Path (Join-Path $OutputRoot "minecraft-game-args.txt") -Text (($gameArgs -join "`r`n") + "`r`n")

$manifestPath = Join-Path $OutputRoot "download-manifest.json"
$baseManifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifestEntries = New-Object System.Collections.Generic.List[object]
foreach ($entry in @($baseManifest.Entries)) {
    Add-ManifestEntryUnique `
        -Entries $manifestEntries `
        -RemoteUrl ([string]$entry.RemoteUrl) `
        -RelativePath ([string]$entry.LocalRelativePath) `
        -Size ([long]$entry.ExpectedSizeBytes)
}

$neoForgeLibraries = New-Object System.Collections.Generic.List[string]
foreach ($library in @($neoForgeVersionJson.libraries)) {
    $artifact = $library.downloads.artifact
    if (-not $artifact -or -not $artifact.path) {
        continue
    }

    $relativePath = [string]$artifact.path
    $originalLocalRelative = Join-Path "libraries" ($relativePath.Replace("/", "\"))
    $localRelative = Get-NeoForgeLocalLibraryPath -LibraryName ([string]$library.name) -ArtifactPath $relativePath
    if ($localRelative -ne $originalLocalRelative) {
        $originalFullPath = Join-Path $OutputRoot $originalLocalRelative
        $shortFullPath = Join-Path $OutputRoot $localRelative
        if (Test-Path $originalFullPath) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $shortFullPath) | Out-Null
            Copy-Item -LiteralPath $originalFullPath -Destination $shortFullPath -Force
            $longVersionDir = Split-Path -Parent $originalFullPath
            if (Test-Path $longVersionDir) {
                Remove-Item -LiteralPath $longVersionDir -Recurse -Force
            }
        }
    }
    Add-ManifestEntryUnique `
        -Entries $manifestEntries `
        -RemoteUrl ([string]$artifact.url) `
        -RelativePath $localRelative `
        -Size ([long]$artifact.size)
    $neoForgeLibraries.Add($localRelative.Replace("/", "\"))
}

$neoForgeLibRoot = "libraries\net\neoforged\$neoForgeArtifactId\$NeoForgeVersion"
$neoForgeGeneratedClientJar = "$neoForgeLibRoot\$neoForgeArtifactId-$NeoForgeVersion-client.jar"
$neoForgeUniversalJar = "$neoForgeLibRoot\$neoForgeArtifactId-$NeoForgeVersion-universal.jar"
$generatedClientFullPath = Join-Path $OutputRoot $neoForgeGeneratedClientJar
$staleProfileJarFullPath = Join-Path $OutputRoot "versions\$neoForgeLauncherVersion\$neoForgeLauncherVersion.jar"
if (Test-Path $staleProfileJarFullPath) {
    Remove-Item -LiteralPath $staleProfileJarFullPath -Force
}
if (-not (Test-Path $generatedClientFullPath)) {
    throw "$loaderName generated client jar missing: $generatedClientFullPath"
}
Add-ManifestEntryUnique `
    -Entries $manifestEntries `
    -RemoteUrl "generated:$loaderSlug-client" `
    -RelativePath $neoForgeGeneratedClientJar
Add-ManifestEntryUnique `
    -Entries $manifestEntries `
    -RemoteUrl "https://maven.neoforged.net/releases/net/neoforged/$neoForgeArtifactId/$NeoForgeVersion/$neoForgeArtifactId-$NeoForgeVersion-universal.jar" `
    -RelativePath $neoForgeUniversalJar
$neoForgeLibraries.Add($neoForgeGeneratedClientJar)
$neoForgeLibraries.Add($neoForgeUniversalJar)

$downloadManifestOut = [ordered]@{
    MinecraftVersion = $MinecraftVersion
    ModLoader = $loaderName
    NeoForgeVersion = $NeoForgeVersion
    NeoForgeArtifactId = $neoForgeArtifactId
    NeoForgeProfileVersion = $neoForgeLauncherVersion
    Entries = $manifestEntries
}
Write-Utf8NoBom -Path $manifestPath -Text (($downloadManifestOut | ConvertTo-Json -Depth 12) + "`r`n")

$packagedVersion = if ($neoForgeArtifactId -eq "forge") { $neoForgeLauncherVersion } else { $profileId }
$summaryPath = Join-Path $OutputRoot "staging-summary.json"
$summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
$summary | Add-Member -NotePropertyName Version -NotePropertyValue $packagedVersion -Force
$summary | Add-Member -NotePropertyName DisplayName -NotePropertyValue "$MinecraftVersion $loaderName" -Force
$summary | Add-Member -NotePropertyName BaseMinecraftVersion -NotePropertyValue $MinecraftVersion -Force
$summary | Add-Member -NotePropertyName OutputRoot -NotePropertyValue $OutputRoot -Force
$summary | Add-Member -NotePropertyName ModLoader -NotePropertyValue $loaderName -Force
$summary | Add-Member -NotePropertyName NeoForgeVersion -NotePropertyValue $NeoForgeVersion -Force
$summary | Add-Member -NotePropertyName NeoForgeArtifactId -NotePropertyValue $neoForgeArtifactId -Force
$summary | Add-Member -NotePropertyName NeoForgeProfileVersion -NotePropertyValue $neoForgeLauncherVersion -Force
$summary | Add-Member -NotePropertyName LauncherVersionName -NotePropertyValue $neoForgeLauncherVersion -Force
$summary | Add-Member -NotePropertyName MainClass -NotePropertyValue "cpw.mods.bootstraplauncher.BootstrapLauncher" -Force
$summary | Add-Member -NotePropertyName NeoForgeProfile -NotePropertyValue "versions\$neoForgeLauncherVersion\$neoForgeLauncherVersion.json" -Force
$summary | Add-Member -NotePropertyName ProfileJvmArgs -NotePropertyValue "minecraft-jvm-args.txt" -Force
$summary | Add-Member -NotePropertyName ProfileGameArgs -NotePropertyValue "minecraft-game-args.txt" -Force
$summary | Add-Member -NotePropertyName NeoForgeLibraries -NotePropertyValue $neoForgeLibraries.Count -Force
Write-Utf8NoBom -Path $summaryPath -Text (($summary | ConvertTo-Json -Depth 8) + "`r`n")

Write-Host ""
Write-Host "$loaderName profile staged:"
Write-Host "  $OutputRoot"
Write-Host "Minecraft: $MinecraftVersion"
Write-Host "${loaderName}: $NeoForgeVersion"
Write-Host "Profile: $profileId"
Write-Host "$loaderName libraries: $($neoForgeLibraries.Count)"
