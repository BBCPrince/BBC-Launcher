param(
    [string]$Version,
    [string]$OutputRoot,
    [switch]$IncludeAssets,
    [switch]$DownloadJava,
    [string]$JavaHome,
    [int]$JavaMajorVersion,
    [switch]$Clean,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$configPath = Join-Path $repoRoot "src\BBCLauncher\launcher.config.json"
if (-not $Version) {
    $Version = (Get-Content $configPath -Raw | ConvertFrom-Json).TargetMinecraftVersion
}

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot "local-test-files\LocalState"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
if ($Clean -and (Test-Path $OutputRoot)) {
    $localTestRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local-test-files"))
    $targetWithSlash = $OutputRoot.TrimEnd('\') + '\'
    $allowedRootWithSlash = $localTestRoot.TrimEnd('\') + '\'
    if (-not $targetWithSlash.StartsWith($allowedRootWithSlash, [System.StringComparison]::OrdinalIgnoreCase) -or
        $targetWithSlash -eq $allowedRootWithSlash) {
        throw "Refusing to clean output root outside local-test-files: $OutputRoot"
    }

    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$manifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
$resourcesUrl = "https://resources.download.minecraft.net"
$manifestEntries = New-Object System.Collections.Generic.List[object]

function Get-Sha1 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA1).Hash.ToLowerInvariant()
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Save-Json {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Depth = 30
    )

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $Value | ConvertTo-Json -Depth $Depth | Set-Content -Path $Path -Encoding UTF8
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

function Add-ManifestEntry {
    param(
        [string]$RemoteUrl,
        [string]$RelativePath,
        [long]$Size = 0
    )

    $fullPath = Join-Path $OutputRoot $RelativePath
    $manifestEntries.Add([ordered]@{
        RemoteUrl = $RemoteUrl
        LocalRelativePath = $RelativePath.Replace("/", "\")
        ExpectedSizeBytes = if ($Size -gt 0) { $Size } else { (Get-Item $fullPath).Length }
        Sha256 = Get-Sha256 $fullPath
        RequiredBeforeLaunch = $true
    })
}

function Test-LibraryAllowed {
    param($Library)

    if (-not $Library.rules) {
        return $true
    }

    $allowed = $false
    foreach ($rule in @($Library.rules)) {
        $matches = $true
        if ($rule.os -and $rule.os.name -and $rule.os.name -ne "windows") {
            $matches = $false
        }
        if ($rule.features) {
            $matches = $false
        }
        if ($matches) {
            $allowed = $rule.action -eq "allow"
        }
    }

    return $allowed
}

function Expand-NativeArchive {
    param(
        [string]$ArchivePath,
        [string]$Destination
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        foreach ($entry in $archive.Entries) {
            if (-not $entry.FullName.EndsWith(".dll", [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }

            $target = Join-Path $Destination ([System.IO.Path]::GetFileName($entry.FullName))
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    }
    finally {
        $archive.Dispose()
    }
}

function Expand-JnaDispatchLibrary {
    param(
        [string]$JnaJarPath,
        [string]$Destination
    )

    if (-not (Test-Path $JnaJarPath)) {
        return $false
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $archive = [System.IO.Compression.ZipFile]::OpenRead($JnaJarPath)
    try {
        $entry = $archive.Entries |
            Where-Object { $_.FullName -eq "com/sun/jna/win32-x86-64/jnidispatch.dll" } |
            Select-Object -First 1

        if (-not $entry) {
            return $false
        }

        $target = Join-Path $Destination "jnidispatch.dll"
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        return $true
    }
    finally {
        $archive.Dispose()
    }
}

function Get-RecommendedJavaMajorVersion {
    param([string]$MinecraftVersion)

    $match = [regex]::Match($MinecraftVersion, '^1\.(\d+)')
    if ($match.Success) {
        $minor = [int]$match.Groups[1].Value
        if ($minor -le 16) {
            return 8
        }
        if ($minor -le 20) {
            if ($MinecraftVersion -match '^1\.20\.(5|6)') {
                return 21
            }
            return 17
        }
    }

    return 21
}

function Install-JavaRuntime {
    param(
        [string]$SourceJavaHome,
        [int]$MajorVersion
    )

    $target = Join-Path $OutputRoot "runtime\jre"
    if ($SourceJavaHome) {
        $javaExe = Join-Path $SourceJavaHome "bin\java.exe"
        if (-not (Test-Path $javaExe)) {
            throw "JavaHome does not contain bin\java.exe: $SourceJavaHome"
        }

        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -Path (Join-Path $SourceJavaHome "*") -Destination $target -Recurse -Force
        return
    }

    $javaExe = Join-Path $target "bin\java.exe"
    if ((Test-Path $javaExe) -and -not $Force) {
        return
    }

    $downloadDir = Join-Path $OutputRoot "_downloads"
    if ($MajorVersion -le 0) {
        $MajorVersion = Get-RecommendedJavaMajorVersion -MinecraftVersion $Version
    }

    $extractDir = Join-Path $downloadDir "java-$MajorVersion-extract"
    $zipPath = Join-Path $downloadDir "temurin-jre-$MajorVersion-windows-x64.zip"
    $javaUrl = "https://api.adoptium.net/v3/binary/latest/$MajorVersion/ga/windows/x64/jre/hotspot/normal/eclipse"

    New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
    Save-Download -Url $javaUrl -Path $zipPath

    if (Test-Path $extractDir) {
        Remove-Item -LiteralPath $extractDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

    $extractedJava = Get-ChildItem -Path $extractDir -Recurse -Filter java.exe |
        Where-Object { $_.FullName -like "*\bin\java.exe" } |
        Select-Object -First 1

    if (-not $extractedJava) {
        throw "Downloaded Java runtime did not contain bin\java.exe"
    }

    $runtimeRoot = Split-Path -Parent (Split-Path -Parent $extractedJava.FullName)
    if (Test-Path $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $runtimeRoot "*") -Destination $target -Recurse -Force
}

Write-Host "Fetching official Minecraft metadata for $Version"
$versionManifest = Invoke-RestMethod -Uri $manifestUrl
$versionRef = $versionManifest.versions | Where-Object { $_.id -eq $Version } | Select-Object -First 1
if (-not $versionRef) {
    throw "Minecraft version not found in official launcher manifest: $Version"
}

$resolvedJavaMajorVersion = if ($JavaMajorVersion -gt 0) {
    $JavaMajorVersion
} else {
    Get-RecommendedJavaMajorVersion -MinecraftVersion $Version
}

$versionJson = Invoke-RestMethod -Uri $versionRef.url
Save-Json -Value $versionJson -Path (Join-Path $OutputRoot "versions\$Version\$Version.json")

$client = $versionJson.downloads.client
Save-Download -Url $client.url -Path (Join-Path $OutputRoot "client.jar") -Sha1 $client.sha1 -Size $client.size
Add-ManifestEntry -RemoteUrl $client.url -RelativePath "client.jar" -Size $client.size

$libraryCount = 0
$nativeCount = 0
$jnaJarPath = $null
foreach ($library in @($versionJson.libraries)) {
    if (-not (Test-LibraryAllowed $library)) {
        continue
    }

    if ($library.downloads.artifact) {
        $artifact = $library.downloads.artifact
        $relative = Join-Path "libraries" $artifact.path
        $artifactPath = Join-Path $OutputRoot $relative
        Save-Download -Url $artifact.url -Path $artifactPath -Sha1 $artifact.sha1 -Size $artifact.size
        Add-ManifestEntry -RemoteUrl $artifact.url -RelativePath $relative -Size $artifact.size
        $artifactFile = [System.IO.Path]::GetFileName($artifact.path)
        if ($artifactFile -eq "jna-5.17.0.jar") {
            $jnaJarPath = $artifactPath
        }
        if ($artifactFile.EndsWith("-natives-windows.jar", [System.StringComparison]::OrdinalIgnoreCase)) {
            Expand-NativeArchive -ArchivePath $artifactPath -Destination (Join-Path $OutputRoot "native")
            $nativeCount++
        }
        $libraryCount++
    }

    $classifiers = $library.downloads.classifiers
    $native = $null
    if ($classifiers) {
        if ($classifiers."natives-windows-x86_64") {
            $native = $classifiers."natives-windows-x86_64"
        }
        elseif ($classifiers."natives-windows") {
            $native = $classifiers."natives-windows"
        }
    }

    if ($native) {
        $nativeArchive = Join-Path $OutputRoot (Join-Path "libraries" $native.path)
        Save-Download -Url $native.url -Path $nativeArchive -Sha1 $native.sha1 -Size $native.size
        Add-ManifestEntry -RemoteUrl $native.url -RelativePath (Join-Path "libraries" $native.path) -Size $native.size
        Expand-NativeArchive -ArchivePath $nativeArchive -Destination (Join-Path $OutputRoot "native")
        $nativeCount++
    }
}

if ($jnaJarPath -and (Expand-JnaDispatchLibrary -JnaJarPath $jnaJarPath -Destination (Join-Path $OutputRoot "native"))) {
    $nativeCount++
}

$assetIndex = $versionJson.assetIndex
$assetIndexRelative = "assets\indexes\$($assetIndex.id).json"
$assetIndexPath = Join-Path $OutputRoot $assetIndexRelative
Save-Download -Url $assetIndex.url -Path $assetIndexPath -Sha1 $assetIndex.sha1 -Size $assetIndex.size
Add-ManifestEntry -RemoteUrl $assetIndex.url -RelativePath $assetIndexRelative -Size $assetIndex.size

$versionAssetAlias = Join-Path $OutputRoot "assets\indexes\$Version.json"
if ($versionAssetAlias -ne $assetIndexPath) {
    Copy-Item -LiteralPath $assetIndexPath -Destination $versionAssetAlias -Force
}

$assetObjectCount = 0
if ($IncludeAssets) {
    $assetIndexJson = Get-Content $assetIndexPath -Raw | ConvertFrom-Json
    $assetTotal = @($assetIndexJson.objects.PSObject.Properties).Count
    foreach ($property in $assetIndexJson.objects.PSObject.Properties) {
        $hash = $property.Value.hash
        $size = [long]$property.Value.size
        $prefix = $hash.Substring(0, 2)
        $relative = "assets\objects\$prefix\$hash"
        Save-Download -Url "$resourcesUrl/$prefix/$hash" -Path (Join-Path $OutputRoot $relative) -Sha1 $hash -Size $size -Quiet
        $assetObjectCount++
        if (($assetObjectCount % 250) -eq 0 -or $assetObjectCount -eq $assetTotal) {
            Write-Host "Assets downloaded/verified: $assetObjectCount / $assetTotal"
        }
    }
}

$bridgeSource = Join-Path $repoRoot "native\graphics_bridge.dll"
if (Test-Path $bridgeSource) {
    $bridgeTarget = Join-Path $OutputRoot "native\graphics_bridge.dll"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $bridgeTarget) | Out-Null
    Copy-Item -LiteralPath $bridgeSource -Destination $bridgeTarget -Force
}

$openAlBridgeSource = Join-Path $repoRoot "native\xbox-openal.dll"
if (Test-Path $openAlBridgeSource) {
    $nativeOutput = Join-Path $OutputRoot "native"
    New-Item -ItemType Directory -Force -Path $nativeOutput | Out-Null
    Copy-Item -LiteralPath $openAlBridgeSource -Destination (Join-Path $nativeOutput "OpenAL32.dll") -Force
    Copy-Item -LiteralPath $openAlBridgeSource -Destination (Join-Path $nativeOutput "OpenAL64.dll") -Force
}

if ($DownloadJava -or $JavaHome) {
    Install-JavaRuntime -SourceJavaHome $JavaHome -MajorVersion $resolvedJavaMajorVersion
}

$downloadManifest = [ordered]@{
    MinecraftVersion = $Version
    Entries = $manifestEntries
}
Save-Json -Value $downloadManifest -Path (Join-Path $OutputRoot "download-manifest.json") -Depth 12

$summary = [ordered]@{
    Version = $Version
    OutputRoot = $OutputRoot
    ClientJar = "client.jar"
    LibraryArtifacts = $libraryCount
    NativeArchivesExtracted = $nativeCount
    AssetIndex = $assetIndex.id
    AssetObjectsDownloaded = $assetObjectCount
    JavaRuntimeMajor = $resolvedJavaMajorVersion
    JavaRuntimePresent = Test-Path (Join-Path $OutputRoot "runtime\jre\bin\java.exe")
    Manifest = "download-manifest.json"
}
Save-Json -Value $summary -Path (Join-Path $OutputRoot "staging-summary.json") -Depth 8

Write-Host ""
Write-Host "Minecraft local test files staged:"
Write-Host "  $OutputRoot"
Write-Host "Libraries: $libraryCount"
Write-Host "Native archives extracted: $nativeCount"
Write-Host "Asset objects: $assetObjectCount"
Write-Host "Java runtime major: $resolvedJavaMajorVersion"
Write-Host "Java runtime present: $($summary["JavaRuntimePresent"])"
Write-Host ""
Write-Host "Copy this folder's contents into the app LocalState folder on the Xbox test device."
