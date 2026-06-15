param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$IncludeMinecraftPayload,
    [switch]$Unsigned,
    [string]$CertificateSubject = "CN=Developer",
    [string]$CertificatePassword = "BBCLauncherTest!"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$project = Join-Path $repoRoot "native\corewindow-probe\CoreWindowProbe.vcxproj"
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
            -FriendlyName "BBC Launcher local test signing" `
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

if (-not (Test-Path (Join-Path $repoRoot "native\mesa-uwp\opengl32.dll"))) {
    throw "Mesa-UWP runtime is missing. Expected native\mesa-uwp\opengl32.dll"
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

$buildProps = @()
if ($IncludeMinecraftPayload) {
    $payloadRoot = Join-Path $repoRoot "local-test-files\LocalState"
    if (-not (Test-Path (Join-Path $payloadRoot "client.jar"))) {
        throw "Minecraft payload missing. Run scripts\Get-MinecraftFiles.ps1 first."
    }

    $buildProps += "/p:IncludeMinecraftPayload=true"
}

$payloadSuffix = if ($IncludeMinecraftPayload) { "_WithMinecraft" } else { "" }
$packageDirName = "AppPackages\CoreWindowProbe${payloadSuffix}_${Configuration}_$(Get-Date -Format yyyyMMddHHmmss)\"

& $msbuild $project /restore /t:Rebuild /p:Configuration=$Configuration /p:Platform=x64 "/p:AppxPackageDir=$packageDirName" "/p:AppxBundle=Never" "/p:UapAppxPackageBuildMode=SideLoadOnly" @buildProps @signingProps /v:minimal /nr:false
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

$artifactName = if ($IncludeMinecraftPayload) {
    "BBCLauncher_1.0.202.0_VisibleLaunch_WithMinecraftPayload_Test"
} else {
    "BBCLauncher_1.0.202.0_VisibleLaunch_Test"
}
$artifactDir = Join-Path $repoRoot "artifacts\xbox\$artifactName"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$artifactPath = Join-Path $artifactDir $package.Name
Copy-Item -LiteralPath $package.FullName -Destination $artifactPath -Force
$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath

Write-Host ""
Write-Host "Native CoreWindow probe package:"
Write-Host "  $artifactPath"
Write-Host "SHA256:"
Write-Host "  $($hash.Hash)"
if (-not $Unsigned) {
    Write-Host "Trust certificate:"
    Write-Host "  $certCer"
}
Write-Host "Expected fixed-build marker:"
Write-Host "  VISIBLE-LAUNCH-FIX"
