param(
    [ValidateSet("SeriesX", "SeriesX-XboxUi", "GraphicsProbe", "XboxOneCompat")]
    [string]$Variant = "SeriesX",
    [string]$Configuration = "Release"
)

$projectDir = Join-Path $PSScriptRoot "..\src\BBCLauncher"
$configPath = Join-Path $projectDir "launcher.config.json"
$config = Get-Content $configPath -Raw | ConvertFrom-Json

switch ($Variant) {
    "SeriesX" {
        $config.EnableResolutionPicker = $true
        $config.IncludeStandardControllerMod = $true
        $config.IncludeXboxUiMod = $false
        $config.GraphicsProbeMode = $false
        $buildVariant = ""
    }
    "SeriesX-XboxUi" {
        $config.EnableResolutionPicker = $true
        $config.IncludeStandardControllerMod = $false
        $config.IncludeXboxUiMod = $true
        $config.GraphicsProbeMode = $false
        $buildVariant = ""
    }
    "GraphicsProbe" {
        $config.EnableResolutionPicker = $false
        $config.GraphicsProbeMode = $true
        $buildVariant = "GRAPHICS_PROBE"
    }
    "XboxOneCompat" {
        $config.DefaultHeapMegabytes = 2048
        $config.SoftwareRenderingFallback = $true
        $buildVariant = ""
    }
}

$config | ConvertTo-Json -Depth 6 | Set-Content $configPath -Encoding UTF8

$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null | Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild not found. Install Visual Studio 2022 with UWP and Xbox (GDK) workloads."
}

$sln = Join-Path $PSScriptRoot "..\BBCLauncher.sln"
$props = @()
if ($buildVariant) { $props += "/p:BuildVariant=$buildVariant" }

& $msbuild $sln /p:Configuration=$Configuration /p:Platform=x64 @props
Write-Host "Built variant: $Variant"
