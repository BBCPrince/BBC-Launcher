#Requires -RunAsAdministrator
<#
.SYNOPSIS
Installs the full "Universal Windows Platform development" workload for VS 2022.
The .vsconfig component list alone often does NOT install the UWP project system VS needs to open .csproj files.
#>
$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$setup = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"

if (-not (Test-Path $setup)) {
    throw "Visual Studio Installer not found at $setup"
}

$installPath = & $vswhere -latest -property installationPath
if (-not $installPath) {
    throw "No Visual Studio 2022 installation found."
}

Write-Host "Installing UWP workload into: $installPath"
Write-Host "This can take 10-20 minutes. Do not close the installer window."
Write-Host ""

# Workload ID is required for the IDE project system (guid A5A43C5B-DE2A-4C0C-ADA5-6B8A7A0E5BFA).
$args = @(
    "modify",
    "--installPath", $installPath,
    "--add", "Microsoft.VisualStudio.Workload.Universal",
    "--includeRecommended",
    "--passive",
    "--norestart",
    "--wait"
)

$proc = Start-Process -FilePath $setup -ArgumentList $args -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "Installer exited with code $($proc.ExitCode). Open Visual Studio Installer manually and enable 'Universal Windows Platform development'."
}

Write-Host ""
Write-Host "Verifying UWP project system..."
$uwpTargets = Join-Path $installPath "MSBuild\Microsoft\WindowsXaml\v17.0\Microsoft.Windows.UI.Xaml.CSharp.targets"
if (-not (Test-Path $uwpTargets)) {
    Write-Warning "XAML targets still missing at $uwpTargets"
}

$check = & $vswhere -latest -requires Microsoft.VisualStudio.Workload.Universal -property displayName
if ($check) {
    Write-Host "OK: $check has Universal workload."
} else {
    Write-Warning "Workload verification failed. Use Visual Studio Installer UI."
}

Write-Host ""
Write-Host "Done. Close ALL Visual Studio windows, reopen BBCLauncher.sln, platform x64."
