param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$Unsigned,
    [string]$PayloadRoot,
    [switch]$DownloadMinecraftFilesOnFirstLaunch,
    [switch]$DownloadOnlyAssetObjectsOnFirstLaunch,
    [switch]$SkipNativeRebuild,
    [switch]$True720pBuild,
    [switch]$TrueHdBuild,
    [switch]$True1440pBuild,
    [switch]$True4KBuild,
    [switch]$Target120FpsBuild,
    [switch]$DisableAccountSigninBuild,
    [switch]$DiagnosticMsbuildLog,
    [string]$ExtraPreprocessorDefinitions,
    [string]$CertificateSubject = "CN=Developer",
    [string]$CertificatePassword = "BBCLauncherTest!"
)

$ErrorActionPreference = "Stop"

if ($DownloadMinecraftFilesOnFirstLaunch.IsPresent -and $DownloadOnlyAssetObjectsOnFirstLaunch.IsPresent) {
    throw "Use either -DownloadMinecraftFilesOnFirstLaunch or -DownloadOnlyAssetObjectsOnFirstLaunch, not both."
}
$resolutionBuildCount = @($True720pBuild, $TrueHdBuild, $True1440pBuild, $True4KBuild) |
    Where-Object { $_.IsPresent } |
    Measure-Object |
    Select-Object -ExpandProperty Count
if ($resolutionBuildCount -gt 1) {
    throw "Use only one of -True720pBuild, -TrueHdBuild, -True1440pBuild, or -True4KBuild."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$project = Join-Path $repoRoot "native\corewindow-probe\CoreWindowProbe.vcxproj"
$mesaDir = Join-Path $repoRoot "native\mesa-uwp"
$dxilPath = Join-Path $mesaDir "dxil.dll"
$certDir = Join-Path $repoRoot "local-test-files\certs"
$certPfx = Join-Path $certDir "BBCLauncher-Test.pfx"
$certCer = Join-Path $certDir "BBCLauncher-Test.cer"

function New-TestSigningCertificate {
    param(
        [string]$Subject,
        [string]$PfxPath,
        [string]$CerPath,
        [string]$Password
    )

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $PfxPath) | Out-Null

    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object {
            $_.Subject -eq $Subject -and
            $_.HasPrivateKey -and
            $_.EnhancedKeyUsageList.FriendlyName -contains "Code Signing"
        } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1

    if (-not $cert) {
        $cert = New-SelfSignedCertificate `
            -Type CodeSigningCert `
            -Subject $Subject `
            -FriendlyName "Minecraft Java Test Fixes local test signing" `
            -CertStoreLocation "Cert:\CurrentUser\My" `
            -KeyUsage DigitalSignature `
            -KeyAlgorithm RSA `
            -KeyLength 2048 `
            -NotAfter (Get-Date).AddYears(5)
    }

    if (-not (Test-Path $PfxPath)) {
        $securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
        Export-PfxCertificate -Cert $cert -FilePath $PfxPath -Password $securePassword -Force | Out-Null
    }

    if (-not (Test-Path $CerPath)) {
        Export-Certificate -Cert $cert -FilePath $CerPath -Force | Out-Null
    }

    return $cert
}

if (-not (Test-Path (Join-Path $mesaDir "opengl32.dll"))) {
    throw "Mesa-UWP runtime is missing. Expected native\mesa-uwp\opengl32.dll"
}

& (Join-Path $PSScriptRoot "Get-DxilRuntime.ps1") -TargetPath $dxilPath

$payloadRoot = if ($PayloadRoot) {
    [System.IO.Path]::GetFullPath($PayloadRoot)
} else {
    Join-Path $repoRoot "local-test-files\LocalState"
}
if (-not (Test-Path (Join-Path $payloadRoot "client.jar")) -and -not (Test-Path (Join-Path $payloadRoot "profiles"))) {
    throw "Minecraft payload missing at $payloadRoot. Run scripts\Get-MinecraftFiles.ps1 first, or pass a multi-version payload root containing profiles\."
}

$payloadVersion = "unknown"
$summaryPath = Join-Path $payloadRoot "staging-summary.json"
$multiSummaryPath = Join-Path $payloadRoot "multi-staging-summary.json"
if (Test-Path $multiSummaryPath) {
    try {
        $summary = Get-Content $multiSummaryPath -Raw | ConvertFrom-Json
        if ($summary.Profiles) {
            $payloadVersion = "multi: " + (@($summary.Profiles | ForEach-Object { $_.Version }) -join ", ")
        }
    } catch {
        $payloadVersion = "multi"
    }
}
elseif (Test-Path $summaryPath) {
    try {
        $summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
        if ($summary.Version) {
            $payloadVersion = [string]$summary.Version
        }
    } catch {
        $payloadVersion = "unknown"
    }
}

if (-not $SkipNativeRebuild) {
    Write-Host "Building native bridge DLLs..."
    & (Join-Path $PSScriptRoot "Build-NativeBridge.ps1") -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build-NativeBridge.ps1 failed with exit code $LASTEXITCODE"
    }
}

$jdkPatchJar = Join-Path $repoRoot "native\xbox-jdk-patch.jar"
$jdkLinkPatchJar = Join-Path $repoRoot "native\xbox-jdk-link-patch.jar"
if (-not (Test-Path $jdkPatchJar) -or -not (Test-Path $jdkLinkPatchJar)) {
    Write-Host "Building JDK path patch..."
    & (Join-Path $PSScriptRoot "Build-JdkPatch.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Build-JdkPatch.ps1 failed with exit code $LASTEXITCODE"
    }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null | Select-Object -First 1
}

if (-not $msbuild) {
    throw "MSBuild not found. Install Visual Studio 2022 with UWP workload."
}

$signingProps = @("/p:AppxPackageSigningEnabled=false")
if (-not $Unsigned) {
    $cert = New-TestSigningCertificate `
        -Subject $CertificateSubject `
        -PfxPath $certPfx `
        -CerPath $certCer `
        -Password $CertificatePassword

    $signingProps = @(
        "/p:AppxPackageSigningEnabled=true",
        "/p:PackageCertificateThumbprint=$($cert.Thumbprint)"
    )
}

$extraBuildProps = @()
if (-not [string]::IsNullOrWhiteSpace($ExtraPreprocessorDefinitions)) {
    $escapedExtraPreprocessorDefinitions = $ExtraPreprocessorDefinitions -replace ';', '%3B'
    $extraBuildProps += "/p:ExtraPreprocessorDefinitions=$escapedExtraPreprocessorDefinitions"
}
if ($True720pBuild.IsPresent) {
    $extraBuildProps += "/p:MinecraftXboxTrue720pBuild=true"
}
if ($TrueHdBuild.IsPresent) {
    $extraBuildProps += "/p:MinecraftXboxTrueHdBuild=true"
}
if ($True1440pBuild.IsPresent) {
    $extraBuildProps += "/p:MinecraftXboxTrue1440pBuild=true"
}
if ($True4KBuild.IsPresent) {
    $extraBuildProps += "/p:MinecraftXboxTrue4KBuild=true"
}
if ($Target120FpsBuild.IsPresent) {
    $extraBuildProps += "/p:MinecraftXboxTarget120FpsBuild=true"
}
if ($DisableAccountSigninBuild.IsPresent) {
    $extraBuildProps += "/p:MinecraftXboxDisableAccountSigninBuild=true"
}

$msbuildLogArgs = @("/v:minimal")
if ($DiagnosticMsbuildLog.IsPresent) {
    $msbuildLogDir = Join-Path $repoRoot "artifacts\logs"
    New-Item -ItemType Directory -Force -Path $msbuildLogDir | Out-Null
    $msbuildLogStamp = Get-Date -Format yyyyMMdd-HHmmss
    $msbuildTextLog = Join-Path $msbuildLogDir "MinecraftJavaTestFixes-msbuild-$msbuildLogStamp.log"
    $msbuildBinaryLog = Join-Path $msbuildLogDir "MinecraftJavaTestFixes-msbuild-$msbuildLogStamp.binlog"
    $msbuildLogArgs += @(
        "/flp:logfile=$msbuildTextLog;verbosity=diagnostic",
        "/bl:$msbuildBinaryLog"
    )
}

$packageDirName = "AppPackages\MinecraftJavaTestFixes_${Configuration}_$(Get-Date -Format yyyyMMddHHmmss)\"
$msbuildTarget = "Build"

Write-Host ""
Write-Host "Building MinecraftJavaTestFixes package..."
Write-Host "  Identity: MinecraftJavaTestFixes"
Write-Host "  Payload: $payloadVersion ($payloadRoot)"
Write-Host "  MSBuild target: $msbuildTarget"
Write-Host "  First-launch game downloads: $($DownloadMinecraftFilesOnFirstLaunch.IsPresent)"
Write-Host "  First-launch asset-object downloads only: $($DownloadOnlyAssetObjectsOnFirstLaunch.IsPresent)"
if ($extraBuildProps.Count -gt 0) {
    Write-Host "  Extra build properties: $($extraBuildProps -join ' ')"
}
if ($DiagnosticMsbuildLog.IsPresent) {
    Write-Host "  MSBuild text log: $msbuildTextLog"
    Write-Host "  MSBuild binary log: $msbuildBinaryLog"
}
Write-Host "  Fixes: disabled EGL warmup export, raw CoreWindow surface first, eglSwapBuffers diagnostics"
Write-Host ""

& $msbuild $project /restore "/t:$msbuildTarget" /p:Configuration=$Configuration /p:Platform=x64 `
    "/p:AppxPackageDir=$packageDirName" `
    "/p:AppxBundle=Never" `
    "/p:UapAppxPackageBuildMode=SideLoadOnly" `
    "/p:IncludeMinecraftPayload=true" `
    "/p:MinecraftPayloadRoot=$payloadRoot" `
    "/p:DownloadMinecraftFilesOnFirstLaunch=$($DownloadMinecraftFilesOnFirstLaunch.IsPresent.ToString().ToLowerInvariant())" `
    "/p:DownloadOnlyAssetObjectsOnFirstLaunch=$($DownloadOnlyAssetObjectsOnFirstLaunch.IsPresent.ToString().ToLowerInvariant())" `
    "/p:TestFixesBuild=true" `
    @extraBuildProps `
    @signingProps @msbuildLogArgs /nr:false

if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}

$packageRoot = Join-Path $repoRoot "native\corewindow-probe\AppPackages"
$package = Get-ChildItem -Path $packageRoot -Recurse -Include "*.msix","*.appx","*.msixbundle","*.appxbundle" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $package) {
    throw "Build completed but no package was found under $packageRoot"
}

$artifactDir = Join-Path $repoRoot "artifacts\xbox\MinecraftJavaTestFixes"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$artifactPath = Join-Path $artifactDir $package.Name
Copy-Item -LiteralPath $package.FullName -Destination $artifactPath -Force
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath

Write-Host ""
Write-Host "MinecraftJavaTestFixes package:"
Write-Host "  $artifactPath"
Write-Host "SHA256:"
Write-Host "  $($hash.Hash)"
if (-not $Unsigned) {
    Write-Host "Trust certificate:"
    Write-Host "  $certCer"
}
Write-Host ""
Write-Host "Install on Xbox (Developer Mode), then check logs under LocalState\logs\:"
Write-Host "  native-corewindow-probe.log"
Write-Host "  native-glfw-from-native-host.log"
Write-Host "  native-minecraft.log"
Write-Host "Expected fixed-build marker:"
Write-Host "  VISIBLE-LAUNCH-FIX"
