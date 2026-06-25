param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$PayloadRoot,
    [switch]$SkipNativeRebuild,
    [switch]$Target120FpsBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $PayloadRoot) {
    $PayloadRoot = Join-Path $repoRoot "local-test-files\LocalState-MultiVersion-320Style"
}

$PayloadRoot = [System.IO.Path]::GetFullPath($PayloadRoot)
$releaseDir = Join-Path $repoRoot "artifacts\xbox\PublicRelease"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

$variants = @(
    @{ Name = "720p"; Switch = "-True720pBuild" },
    @{ Name = "1080p"; Switch = "-TrueHdBuild" },
    @{ Name = "1440p"; Switch = "-True1440pBuild" },
    @{ Name = "4k"; Switch = "-True4KBuild" }
)

$built = New-Object System.Collections.Generic.List[object]
$skipNative = [bool]$SkipNativeRebuild
foreach ($variant in $variants) {
    $buildParams = @{
        Configuration = $Configuration
        PayloadRoot = $PayloadRoot
        DownloadMinecraftFilesOnFirstLaunch = $true
    }

    if ($skipNative) {
        $buildParams.SkipNativeRebuild = $true
    }
    if ($Target120FpsBuild.IsPresent) {
        $buildParams.Target120FpsBuild = $true
    }

    switch ($variant.Name) {
        "720p" { $buildParams.True720pBuild = $true }
        "1080p" { $buildParams.TrueHdBuild = $true }
        "1440p" { $buildParams.True1440pBuild = $true }
        "4k" { $buildParams.True4KBuild = $true }
    }

    & (Join-Path $PSScriptRoot "Build-MinecraftJavaTestFixes.ps1") @buildParams
    if ($LASTEXITCODE -ne 0) {
        throw "Build-MinecraftJavaTestFixes.ps1 failed for $($variant.Name) with exit code $LASTEXITCODE"
    }

    $source = Get-ChildItem -LiteralPath (Join-Path $repoRoot "artifacts\xbox\MinecraftJavaTestFixes") -Filter "*.msix" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $source) {
        throw "No MSIX was produced for $($variant.Name)"
    }

    $targetName = "BBCLauncherbeta_1.0.407.0_x64_$($variant.Name)_public.msix"
    $targetPath = Join-Path $releaseDir $targetName
    Copy-Item -LiteralPath $source.FullName -Destination $targetPath -Force
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $targetPath
    $built.Add([ordered]@{
        Variant = $variant.Name
        Path = $targetPath
        SHA256 = $hash.Hash
        Bytes = (Get-Item -LiteralPath $targetPath).Length
    })

    $skipNative = $true
}

$manifestPath = Join-Path $releaseDir "BBCLauncherbeta_1.0.407.0_public_sha256.json"
$built | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ""
Write-Host "Public release variants:"
foreach ($item in $built) {
    Write-Host "  $($item.Variant): $($item.Path)"
    Write-Host "    SHA256: $($item.SHA256)"
}
Write-Host "Manifest:"
Write-Host "  $manifestPath"
