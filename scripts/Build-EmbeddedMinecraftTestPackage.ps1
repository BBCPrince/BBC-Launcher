param(
    [string]$Version,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$IncludeAssets,
    [switch]$SkipStagingRefresh,
    [switch]$Unsigned,
    [string]$CertificateSubject = "CN=Developer",
    [string]$CertificatePassword = "BBCLauncherTest!"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sln = Join-Path $repoRoot "BBCLauncher.sln"
$stageScript = Join-Path $PSScriptRoot "Get-MinecraftFiles.ps1"
$payloadRoot = Join-Path $repoRoot "local-test-files\LocalState"
$payloadZip = Join-Path $repoRoot "local-test-files\minecraft-localstate-payload.zip"
$certDir = Join-Path $repoRoot "local-test-files\certs"
$certPfx = Join-Path $certDir "BBCLauncher-Test.pfx"
$certCer = Join-Path $certDir "BBCLauncher-Test.cer"

function Get-RelativePath {
    param(
        [string]$Root,
        [string]$Path
    )

    $rootUri = [Uri]((Resolve-Path $Root).Path.TrimEnd('\') + '\')
    $pathUri = [Uri]((Resolve-Path $Path).Path)
    return [Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function New-LocalStatePayloadZip {
    param(
        [string]$SourceRoot,
        [string]$DestinationZip
    )

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestinationZip) | Out-Null
    if (Test-Path $DestinationZip) {
        Remove-Item -LiteralPath $DestinationZip -Force
    }

    $zip = [System.IO.Compression.ZipFile]::Open($DestinationZip, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $files = Get-ChildItem -Path $SourceRoot -Recurse -File |
            Where-Object { $_.FullName -notlike "*\_downloads\*" }

        foreach ($file in $files) {
            $relative = (Get-RelativePath -Root $SourceRoot -Path $file.FullName).Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip,
                $file.FullName,
                $relative,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
    finally {
        $zip.Dispose()
    }
}

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

if (-not $SkipStagingRefresh) {
    $stageArgs = @{
        DownloadJava = $true
    }
    if ($Version) {
        $stageArgs.Version = $Version
    }
    if ($IncludeAssets) {
        $stageArgs.IncludeAssets = $true
    }

    & $stageScript @stageArgs
}

if (-not (Test-Path (Join-Path $payloadRoot "client.jar"))) {
    throw "Staged Minecraft payload was not found. Run scripts\Get-MinecraftFiles.ps1 first."
}

$bridge = Join-Path $repoRoot "native\graphics_bridge.dll"
$glfwStub = Join-Path $repoRoot "native\xbox-glfw.dll"
$openglStub = Join-Path $repoRoot "native\xbox-opengl.dll"
$openalStub = Join-Path $repoRoot "native\xbox-openal.dll"
$mesaUwpRuntime = Join-Path $repoRoot "native\mesa-uwp"
$glon12Runtime = Join-Path $repoRoot "native\glon12"
if (-not (Test-Path $bridge) -or -not (Test-Path $glfwStub) -or -not (Test-Path $openglStub) -or -not (Test-Path $openalStub)) {
    & (Join-Path $PSScriptRoot "Build-NativeBridge.ps1") -Configuration Release
}

$payloadNativeDir = Join-Path $payloadRoot "native"
New-Item -ItemType Directory -Force -Path $payloadNativeDir | Out-Null
Copy-Item -LiteralPath $bridge   -Destination (Join-Path $payloadNativeDir "graphics_bridge.dll") -Force
if (Test-Path $glfwStub) {
    Copy-Item -LiteralPath $glfwStub -Destination (Join-Path $payloadNativeDir "xbox-glfw.dll") -Force
}
if (Test-Path $openglStub) {
    Copy-Item -LiteralPath $openglStub -Destination (Join-Path $payloadNativeDir "xbox-opengl.dll") -Force
}
if (Test-Path $mesaUwpRuntime) {
    $payloadMesaUwpDir = Join-Path $payloadNativeDir "mesa-uwp"
    New-Item -ItemType Directory -Force -Path $payloadMesaUwpDir | Out-Null
    Get-ChildItem -LiteralPath $mesaUwpRuntime -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $payloadMesaUwpDir -Recurse -Force
    }
    Write-Host ("Copied Mesa-UWP runtime -> {0}" -f $payloadMesaUwpDir)
}
if (Test-Path $glon12Runtime) {
    $payloadGlon12Dir = Join-Path $payloadNativeDir "glon12"
    New-Item -ItemType Directory -Force -Path $payloadGlon12Dir | Out-Null
    Get-ChildItem -LiteralPath $glon12Runtime -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $payloadGlon12Dir -Recurse -Force
    }
    Write-Host ("Copied GLon12 runtime -> {0}" -f $payloadGlon12Dir)
}
if (Test-Path $openalStub) {
    Copy-Item -LiteralPath $openalStub -Destination (Join-Path $payloadNativeDir "xbox-openal.dll") -Force
}

New-LocalStatePayloadZip -SourceRoot $payloadRoot -DestinationZip $payloadZip

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

$packageDirName = "AppPackages\EmbeddedMinecraft_${Configuration}_$(Get-Date -Format yyyyMMddHHmmss)\"

& $msbuild $sln /restore /t:Rebuild /p:Configuration=$Configuration /p:Platform=x64 /p:IncludeMinecraftPayload=true "/p:AppxPackageDir=$packageDirName" @signingProps /v:minimal /nr:false
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}

$packageRoot = Join-Path $repoRoot "src\BBCLauncher\AppPackages"
$msix = Get-ChildItem -Path $packageRoot -Recurse -Filter "*.msix" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $msix) {
    throw "Build completed but no MSIX package was found under $packageRoot"
}

Write-Host ""
Write-Host "Embedded Minecraft test package:"
Write-Host "  $($msix.FullName)"
Write-Host "Size MB: $([math]::Round($msix.Length / 1MB, 1))"
Write-Host "Payload root:"
Write-Host "  $payloadRoot"
if (-not $Unsigned) {
    Write-Host "Trust certificate:"
    Write-Host "  $certCer"
}
Write-Host "Expected fixed-build marker:"
Write-Host "  VISIBLE-LAUNCH-FIX"
