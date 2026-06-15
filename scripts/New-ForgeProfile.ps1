param(
    [string]$MinecraftVersion = "1.12.2",
    [string]$ForgeVersion,
    [string]$BaseProfileRoot,
    [string]$OutputRoot,
    [switch]$Clean,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $BaseProfileRoot) {
    $BaseProfileRoot = Join-Path $repoRoot "local-test-files\LocalState-$MinecraftVersion"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "local-test-files\LocalState-$MinecraftVersion-Forge"
}

$BaseProfileRoot = [System.IO.Path]::GetFullPath($BaseProfileRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$localTestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local-test-files"))
$targetWithSlash = $OutputRoot.TrimEnd('\') + '\'
$allowedRootWithSlash = $localTestRoot.TrimEnd('\') + '\'
if (-not $targetWithSlash.StartsWith($allowedRootWithSlash, [System.StringComparison]::OrdinalIgnoreCase) -or
    $targetWithSlash -eq $allowedRootWithSlash) {
    throw "Refusing to create Forge profile outside local-test-files: $OutputRoot"
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
    $Entries.Add([ordered]@{
        RemoteUrl = $RemoteUrl
        LocalRelativePath = $normalized
        ExpectedSizeBytes = if ($Size -gt 0) { $Size } else { (Get-Item $fullPath).Length }
        Sha256 = Get-Sha256 $fullPath
        RequiredBeforeLaunch = $true
    })
}

function Get-LatestForgeVersion {
    param([string]$MinecraftVersion)

    $metadataUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml"
    $metadata = [xml](Invoke-WebRequest -Uri $metadataUrl -UseBasicParsing).Content
    $prefix = "$MinecraftVersion-"
    $matches = @($metadata.metadata.versioning.versions.version | Where-Object { $_.StartsWith($prefix) })
    if ($matches.Count -eq 0) {
        throw "No Forge versions found for Minecraft $MinecraftVersion"
    }

    return @($matches | Sort-Object { [version]($_.Substring($prefix.Length) -replace '-.*$', '') } | Select-Object -Last 1)[0]
}

function Read-ZipText {
    param(
        [System.IO.Compression.ZipArchive]$Archive,
        [string]$EntryName
    )

    $entry = $Archive.GetEntry($EntryName)
    if (-not $entry) {
        throw "Installer is missing $EntryName"
    }

    $reader = New-Object System.IO.StreamReader($entry.Open())
    try {
        return $reader.ReadToEnd()
    }
    finally {
        $reader.Dispose()
    }
}

function Extract-ZipEntry {
    param(
        [System.IO.Compression.ZipArchive]$Archive,
        [string]$EntryName,
        [string]$Destination
    )

    $entry = $Archive.GetEntry($EntryName)
    if (-not $entry) {
        throw "Installer is missing $EntryName"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $Destination, $true)
}

if (-not $ForgeVersion) {
    $forgeMavenVersion = Get-LatestForgeVersion -MinecraftVersion $MinecraftVersion
}
elseif ($ForgeVersion.StartsWith("$MinecraftVersion-")) {
    $forgeMavenVersion = $ForgeVersion
}
else {
    $forgeMavenVersion = "$MinecraftVersion-$ForgeVersion"
}
$forgeOnlyVersion = $forgeMavenVersion.Substring($MinecraftVersion.Length + 1)
$profileId = "$MinecraftVersion-forge-$forgeOnlyVersion"

if ($Clean -and (Test-Path $OutputRoot)) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
Invoke-RobocopyChecked -Source $BaseProfileRoot -Destination $OutputRoot -ExtraArgs @(
    "/XD", "_downloads", "game", "logs", "mods", "mods-library"
)

$downloadRoot = Join-Path $OutputRoot "_downloads\forge"
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
$installerUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/$forgeMavenVersion/forge-$forgeMavenVersion-installer.jar"
$installerPath = Join-Path $downloadRoot "forge-$forgeMavenVersion-installer.jar"
Save-Download -Url $installerUrl -Path $installerPath

Add-Type -AssemblyName System.IO.Compression.FileSystem
$installer = [System.IO.Compression.ZipFile]::OpenRead($installerPath)
try {
    $installProfileJson = Read-ZipText -Archive $installer -EntryName "install_profile.json"
    $versionJsonText = Read-ZipText -Archive $installer -EntryName "version.json"
    $versionJson = $versionJsonText | ConvertFrom-Json

    $versionDir = Join-Path $OutputRoot "versions\$profileId"
    Write-Utf8NoBom -Path (Join-Path $versionDir "$profileId.json") -Text ($versionJsonText + "`r`n")
    Write-Utf8NoBom -Path (Join-Path $versionDir "install_profile.json") -Text ($installProfileJson + "`r`n")

    $forgeJarEntry = "maven/net/minecraftforge/forge/$forgeMavenVersion/forge-$forgeMavenVersion.jar"
    $forgeJarRelative = "libraries\net\minecraftforge\forge\$forgeMavenVersion\forge-$forgeMavenVersion.jar"
    $forgeJarPath = Join-Path $OutputRoot $forgeJarRelative
    Extract-ZipEntry -Archive $installer -EntryName $forgeJarEntry -Destination $forgeJarPath
}
finally {
    $installer.Dispose()
}

$manifestPath = Join-Path $OutputRoot "download-manifest.json"
$baseManifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifestEntries = New-Object System.Collections.Generic.List[object]

$clientEntry = @($baseManifest.Entries | Where-Object { [string]$_.LocalRelativePath -eq "client.jar" } | Select-Object -First 1)
if ($clientEntry.Count -gt 0) {
    $manifestEntries.Add($clientEntry[0])
}

$forgeLibraries = New-Object System.Collections.Generic.List[string]
$forgeUniversalUrl = "https://maven.minecraftforge.net/net/minecraftforge/forge/$forgeMavenVersion/forge-$forgeMavenVersion-universal.jar"
Add-ManifestEntryUnique -Entries $manifestEntries -RemoteUrl $forgeUniversalUrl -RelativePath $forgeJarRelative
$forgeLibraries.Add($forgeJarRelative)

foreach ($library in @($versionJson.libraries)) {
    $artifact = $library.downloads.artifact
    if (-not $artifact -or -not $artifact.path) {
        continue
    }

    $relativePath = [string]$artifact.path
    $localRelative = Join-Path "libraries" ($relativePath.Replace("/", "\"))
    $target = Join-Path $OutputRoot $localRelative
    $url = [string]$artifact.url
    if ([string]::IsNullOrWhiteSpace($url)) {
        $normalizedRelativePath = $localRelative.Replace("/", "\")
        if ([System.String]::Equals($normalizedRelativePath, $forgeJarRelative, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        throw "Forge library has no download URL: $($library.name)"
    }

    Save-Download -Url $url -Path $target -Sha1 ([string]$artifact.sha1) -Size ([long]$artifact.size)
    Add-ManifestEntryUnique -Entries $manifestEntries -RemoteUrl $url -RelativePath $localRelative -Size ([long]$artifact.size)
    $forgeLibraries.Add($localRelative.Replace("/", "\"))
}

foreach ($entry in @($baseManifest.Entries)) {
    Add-ManifestEntryUnique `
        -Entries $manifestEntries `
        -RemoteUrl ([string]$entry.RemoteUrl) `
        -RelativePath ([string]$entry.LocalRelativePath) `
        -Size ([long]$entry.ExpectedSizeBytes)
}

$downloadManifestOut = [ordered]@{
    MinecraftVersion = $MinecraftVersion
    ModLoader = "Forge"
    ForgeVersion = $forgeOnlyVersion
    ForgeProfileVersion = $profileId
    Entries = $manifestEntries
}
Write-Utf8NoBom -Path $manifestPath -Text (($downloadManifestOut | ConvertTo-Json -Depth 12) + "`r`n")

$summaryPath = Join-Path $OutputRoot "staging-summary.json"
$summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
$summary | Add-Member -NotePropertyName Version -NotePropertyValue $profileId -Force
$summary | Add-Member -NotePropertyName DisplayName -NotePropertyValue "$MinecraftVersion Forge" -Force
$summary | Add-Member -NotePropertyName BaseMinecraftVersion -NotePropertyValue $MinecraftVersion -Force
$summary | Add-Member -NotePropertyName OutputRoot -NotePropertyValue $OutputRoot -Force
$summary | Add-Member -NotePropertyName ModLoader -NotePropertyValue "Forge" -Force
$summary | Add-Member -NotePropertyName ForgeVersion -NotePropertyValue $forgeOnlyVersion -Force
$summary | Add-Member -NotePropertyName MainClass -NotePropertyValue "net.minecraft.launchwrapper.Launch" -Force
$summary | Add-Member -NotePropertyName ForgeProfile -NotePropertyValue "versions\$profileId\$profileId.json" -Force
$summary | Add-Member -NotePropertyName ForgeLibraries -NotePropertyValue $forgeLibraries.Count -Force
Write-Utf8NoBom -Path $summaryPath -Text (($summary | ConvertTo-Json -Depth 8) + "`r`n")

Write-Host ""
Write-Host "Forge profile staged:"
Write-Host "  $OutputRoot"
Write-Host "Minecraft: $MinecraftVersion"
Write-Host "Forge: $forgeOnlyVersion"
Write-Host "Profile: $profileId"
Write-Host "Forge libraries: $($forgeLibraries.Count)"
