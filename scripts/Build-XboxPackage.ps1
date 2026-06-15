[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipNative,
    [switch]$IncludeMinecraftDownloadSeedPayload,
    [switch]$Sign,
    [string]$CertificatePath,
    [string]$CertificatePassword
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$projectDir = Join-Path $root "src\BBCLauncher"
$project = Join-Path $projectDir "BBCLauncher.csproj"
$nativeDir = Join-Path $root "native"
$artifactDir = Join-Path $root "artifacts\xbox"

if (-not (Test-Path $project)) {
    throw "Project not found: $project"
}

function Get-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2022."
    }
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2>$null `
        | Select-Object -First 1
    if (-not $msbuild) {
        throw "MSBuild.exe not found. Install Visual Studio 2022 with MSBuild + UWP workload."
    }
    return $msbuild
}

Write-Host "==> Resolving MSBuild" -ForegroundColor Cyan
$msbuild = Get-MSBuild
Write-Host "    MSBuild: $msbuild"

if (-not $SkipNative) {
    Write-Host ""
    Write-Host "==> Building native artifacts (graphics_bridge + Ole32/Pdh shims)" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "Build-NativeBridge.ps1") -Configuration $Configuration | Out-Host

    Write-Host ""
    Write-Host "==> Building Java artifacts (XboxPathProbe.jar + xbox-jdk-patch.jar)" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "Build-JdkPatch.ps1") | Out-Host
} else {
    Write-Host "==> Skipping native build (-SkipNative)" -ForegroundColor Yellow
}

if ($IncludeMinecraftDownloadSeedPayload) {
    $seedManifest = Join-Path $root "local-test-files\LocalState\download-manifest.json"
    $seedRuntime = Join-Path $root "local-test-files\LocalState\runtime\jre\bin\java.exe"
    if (-not (Test-Path $seedManifest)) {
        throw "Download seed payload requested, but local-test-files\LocalState\download-manifest.json is missing. Run scripts\Get-MinecraftFiles.ps1 -DownloadJava first."
    }
    if (-not (Test-Path $seedRuntime)) {
        throw "Download seed payload requested, but local-test-files\LocalState\runtime\jre\bin\java.exe is missing. Run scripts\Get-MinecraftFiles.ps1 -DownloadJava first."
    }

    Write-Host "==> Including Minecraft download seed payload (runtime + manifests, no full game jars/assets)" -ForegroundColor Cyan
}

foreach ($dll in @("graphics_bridge.dll", "Ole32.dll", "Pdh.dll", "xbox-glfw.dll", "xbox-opengl.dll", "xbox-openal.dll")) {
    $path = Join-Path $nativeDir $dll
    if (-not (Test-Path $path)) {
        Write-Warning "Native DLL missing in 'native\': $dll. The MSIX will still build but Xbox runtime probes will WARN."
    }
}

foreach ($jar in @("XboxPathProbe.jar", "xbox-jdk-patch.jar")) {
    $path = Join-Path $nativeDir $jar
    if (-not (Test-Path $path)) {
        Write-Warning "Java artifact missing in 'native\': $jar. The MSIX will build but path-probe / toRealPath() patch will be skipped on Xbox."
    }
}

Write-Host ""
Write-Host "==> Restoring NuGet packages" -ForegroundColor Cyan
& $msbuild $project /t:Restore /p:Configuration=$Configuration /p:Platform=x64 /nologo /v:m | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Restore failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "==> Building MSIX package (Configuration=$Configuration, Platform=x64)" -ForegroundColor Cyan
$signProps = @()
if ($Sign) {
    if (-not $CertificatePath) {
        throw "-Sign requires -CertificatePath <pfx>"
    }
    $signProps += "/p:AppxPackageSigningEnabled=true"
    $signProps += "/p:PackageCertificateKeyFile=$CertificatePath"
    if ($CertificatePassword) {
        $signProps += "/p:PackageCertificatePassword=$CertificatePassword"
    }
} else {
    $signProps += "/p:AppxPackageSigningEnabled=false"
}

& $msbuild $project `
    /t:Build `
    /p:Configuration=$Configuration `
    /p:Platform=x64 `
    /p:IncludeMinecraftDownloadSeedPayload=$($IncludeMinecraftDownloadSeedPayload.IsPresent.ToString().ToLowerInvariant()) `
    /p:AppxBundlePlatforms=x64 `
    /p:UapAppxPackageBuildMode=SideloadOnly `
    /p:AppxBundle=Always `
    /p:GenerateAppxPackageOnBuild=true `
    @signProps `
    /nologo /v:m | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

$packageRoot = Join-Path $projectDir "AppPackages"
if (-not (Test-Path $packageRoot)) {
    throw "AppPackages folder not produced. Check MSBuild output."
}

$produced = Get-ChildItem $packageRoot -Directory `
    | Sort-Object LastWriteTime -Descending `
    | Select-Object -First 1
if (-not $produced) {
    throw "No package folder under $packageRoot"
}

New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$destination = Join-Path $artifactDir $produced.Name
if (Test-Path $destination) {
    Remove-Item $destination -Recurse -Force
}
Copy-Item $produced.FullName $destination -Recurse -Force

$msixArtifacts = Get-ChildItem $destination -Recurse -Include *.msixbundle, *.msix, *.appxbundle, *.appx, *.cer
Write-Host ""
Write-Host "==> Xbox package artifacts" -ForegroundColor Green
foreach ($a in $msixArtifacts) {
    Write-Host ("    {0} ({1:N0} bytes)" -f $a.FullName, $a.Length)
}

Write-Host ""
Write-Host "==> Done. Sideload instructions:" -ForegroundColor Cyan
Write-Host "    1. Put Xbox Series X into Developer Mode (Xbox Dev Mode app)."
Write-Host "    2. Open the Xbox Device Portal on PC: https://<console-ip>:11443"
Write-Host "    3. In 'Home > Add' upload the .msixbundle (and the .cer if signed)."
Write-Host "    4. Wait for 'Installation Complete', then launch from Dev Home."
Write-Host ""
Write-Host "    Resulting folder: $destination"
