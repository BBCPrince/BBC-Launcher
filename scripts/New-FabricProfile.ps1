param(
    [string]$MinecraftVersion = "1.21.11",
    [string]$BaseProfileRoot,
    [string]$OutputRoot,
    [string]$LoaderVersion,
    [switch]$Clean,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $BaseProfileRoot) {
    $BaseProfileRoot = Join-Path $repoRoot "local-test-files\LocalState-$MinecraftVersion"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "local-test-files\LocalState-$MinecraftVersion-Fabric"
}

$BaseProfileRoot = [System.IO.Path]::GetFullPath($BaseProfileRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$localTestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local-test-files"))
$targetWithSlash = $OutputRoot.TrimEnd('\') + '\'
$allowedRootWithSlash = $localTestRoot.TrimEnd('\') + '\'
if (-not $targetWithSlash.StartsWith($allowedRootWithSlash, [System.StringComparison]::OrdinalIgnoreCase) -or
    $targetWithSlash -eq $allowedRootWithSlash) {
    throw "Refusing to create Fabric profile outside local-test-files: $OutputRoot"
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
        [long]$Size = 0,
        [switch]$Quiet
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

    if (-not $Quiet) {
        Write-Host "Downloading $Url"
    }
    Invoke-WebRequest -Uri $Url -OutFile $tempPath

    if ($Size -gt 0 -and ((Get-Item $tempPath).Length -ne $Size)) {
        throw "Downloaded file size mismatch for $Path"
    }

    if ($Sha1 -and ((Get-Sha1 $tempPath) -ne $Sha1.ToLowerInvariant())) {
        throw "Downloaded file hash mismatch for $Path"
    }

    Move-Item -LiteralPath $tempPath -Destination $Path -Force
}

function Convert-MavenNameToRelativePath {
    param([string]$Name)

    $parts = $Name.Split(":")
    if ($parts.Count -lt 3) {
        throw "Unsupported Maven coordinate: $Name"
    }

    $group = $parts[0].Replace(".", "/")
    $artifact = $parts[1]
    $version = $parts[2]
    $classifier = if ($parts.Count -ge 4) { "-" + $parts[3] } else { "" }
    return "$group/$artifact/$version/$artifact-$version$classifier.jar"
}

function Add-ManifestEntry {
    param(
        [System.Collections.Generic.List[object]]$Entries,
        [string]$RemoteUrl,
        [string]$RelativePath,
        [long]$Size = 0
    )

    $fullPath = Join-Path $OutputRoot $RelativePath
    $Entries.Add([ordered]@{
        RemoteUrl = $RemoteUrl
        LocalRelativePath = $RelativePath.Replace("/", "\")
        ExpectedSizeBytes = if ($Size -gt 0) { $Size } else { (Get-Item $fullPath).Length }
        Sha256 = Get-Sha256 $fullPath
        RequiredBeforeLaunch = $true
    })
}

if ($Clean -and (Test-Path $OutputRoot)) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
Invoke-RobocopyChecked -Source $BaseProfileRoot -Destination $OutputRoot -ExtraArgs @(
    "/XD", "_downloads", "game", "logs", "mods", "mods-library"
)

$loaderVersions = Invoke-RestMethod -Uri "https://meta.fabricmc.net/v2/versions/loader/$MinecraftVersion"
if (-not $LoaderVersion) {
    $selected = @($loaderVersions | Where-Object { $_.loader.stable -eq $true } | Select-Object -First 1)
    if (-not $selected -or $selected.Count -eq 0) {
        $selected = @($loaderVersions | Select-Object -First 1)
    }
}
else {
    $selected = @($loaderVersions | Where-Object { $_.loader.version -eq $LoaderVersion } | Select-Object -First 1)
}
if (-not $selected -or $selected.Count -eq 0) {
    throw "Fabric Loader version not found for Minecraft $MinecraftVersion"
}

$LoaderVersion = [string]$selected[0].loader.version
$profileUrl = "https://meta.fabricmc.net/v2/versions/loader/$MinecraftVersion/$LoaderVersion/profile/json"
$fabricProfile = Invoke-RestMethod -Uri $profileUrl
$fabricProfileJson = Invoke-WebRequest -Uri $profileUrl -UseBasicParsing

$manifestPath = Join-Path $OutputRoot "download-manifest.json"
$downloadManifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifestEntries = New-Object System.Collections.Generic.List[object]
foreach ($entry in @($downloadManifest.Entries)) {
    $manifestEntries.Add($entry)
}

$fabricLibraries = New-Object System.Collections.Generic.List[string]
foreach ($library in @($fabricProfile.libraries)) {
    $relative = Convert-MavenNameToRelativePath -Name ([string]$library.name)
    $urlBase = if ($library.url) { [string]$library.url } else { "https://maven.fabricmc.net/" }
    if (-not $urlBase.EndsWith("/")) {
        $urlBase += "/"
    }
    $url = $urlBase + $relative
    $localRelative = Join-Path "libraries" ($relative.Replace("/", "\"))
    $target = Join-Path $OutputRoot $localRelative
    Save-Download -Url $url -Path $target -Sha1 ([string]$library.sha1) -Size ([long]$library.size)
    Add-ManifestEntry -Entries $manifestEntries -RemoteUrl $url -RelativePath $localRelative -Size ([long]$library.size)
    $fabricLibraries.Add($localRelative.Replace("/", "\"))
}

Write-Utf8NoBom -Path (Join-Path $OutputRoot "versions\fabric-loader-$LoaderVersion-$MinecraftVersion\fabric-loader-$LoaderVersion-$MinecraftVersion.json") -Text ($fabricProfileJson.Content + "`r`n")

$downloadManifestOut = [ordered]@{
    MinecraftVersion = $MinecraftVersion
    ModLoader = "Fabric"
    FabricLoaderVersion = $LoaderVersion
    Entries = $manifestEntries
}
Write-Utf8NoBom -Path $manifestPath -Text (($downloadManifestOut | ConvertTo-Json -Depth 12) + "`r`n")

$summaryPath = Join-Path $OutputRoot "staging-summary.json"
$summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
$summary | Add-Member -NotePropertyName OutputRoot -NotePropertyValue $OutputRoot -Force
$summary | Add-Member -NotePropertyName ModLoader -NotePropertyValue "Fabric" -Force
$summary | Add-Member -NotePropertyName FabricLoaderVersion -NotePropertyValue $LoaderVersion -Force
$summary | Add-Member -NotePropertyName MainClass -NotePropertyValue "net.fabricmc.loader.impl.launch.knot.KnotClient" -Force
$summary | Add-Member -NotePropertyName FabricProfile -NotePropertyValue "versions\fabric-loader-$LoaderVersion-$MinecraftVersion\fabric-loader-$LoaderVersion-$MinecraftVersion.json" -Force
$summary | Add-Member -NotePropertyName FabricLibraries -NotePropertyValue $fabricLibraries.Count -Force
Write-Utf8NoBom -Path $summaryPath -Text (($summary | ConvertTo-Json -Depth 8) + "`r`n")

Write-Host ""
Write-Host "Fabric profile staged:"
Write-Host "  $OutputRoot"
Write-Host "Minecraft: $MinecraftVersion"
Write-Host "Fabric Loader: $LoaderVersion"
Write-Host "Fabric libraries: $($fabricLibraries.Count)"
