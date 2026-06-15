param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot ".."
$out = Join-Path $root "native"
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Get-CMakeExecutable {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    throw "cmake not found. Install CMake component in Visual Studio or add cmake.exe to PATH."
}

function Invoke-NativeBuild {
    param(
        [Parameter(Mandatory)] [string]$ProjectName,
        [Parameter(Mandatory)] [string]$SourceDir,
        [Parameter(Mandatory)] [string[]]$OutputDlls
    )

    $buildDir = Join-Path $SourceDir "build"
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    Write-Host ("Configuring {0}" -f $ProjectName)
    & $cmake -S $SourceDir -B $buildDir -G "Visual Studio 17 2022" -A x64 | Out-Host

    Write-Host ("Building {0} ({1})" -f $ProjectName, $Configuration)
    & $cmake --build $buildDir --config $Configuration | Out-Host

    foreach ($dllName in $OutputDlls) {
        $dllPath = Join-Path $buildDir ("{0}\{1}" -f $Configuration, $dllName)
        if (-not (Test-Path $dllPath)) {
            throw ("Build succeeded but DLL not found: {0}" -f $dllPath)
        }

        $destination = Join-Path $out $dllName
        Copy-Item $dllPath $destination -Force
        Write-Host ("Copied {0} -> {1}" -f $dllName, $destination)
    }
}

$cmake = Get-CMakeExecutable

& (Join-Path $PSScriptRoot "Generate-GlfwConstants.ps1")

Invoke-NativeBuild `
    -ProjectName "graphics_bridge" `
    -SourceDir (Join-Path $root "native\graphics-bridge") `
    -OutputDlls @("graphics_bridge.dll")

Invoke-NativeBuild `
    -ProjectName "win_shims" `
    -SourceDir (Join-Path $root "native\win-shims") `
    -OutputDlls @("Ole32.dll", "Pdh.dll")

Invoke-NativeBuild `
    -ProjectName "xbox_glfw" `
    -SourceDir (Join-Path $root "native\xbox-glfw") `
    -OutputDlls @("xbox-glfw.dll")

Invoke-NativeBuild `
    -ProjectName "xbox_opengl" `
    -SourceDir (Join-Path $root "native\xbox-opengl") `
    -OutputDlls @("xbox-opengl.dll")

Invoke-NativeBuild `
    -ProjectName "xbox_openal" `
    -SourceDir (Join-Path $root "native\xbox-openal") `
    -OutputDlls @("xbox-openal.dll")

& (Join-Path $PSScriptRoot "Build-Lwjgl2PatchAgent.ps1") -Configuration $Configuration
& (Join-Path $PSScriptRoot "Build-ModernLanAgent.ps1") -Configuration $Configuration
& (Join-Path $PSScriptRoot "Build-FabricLanDiscoveryMod.ps1") -Configuration $Configuration

Write-Host ""
Write-Host "All native artifacts ready in: $out"
Write-Host "Next: copy these DLLs into Xbox app LocalState\native\ along with launcher assets."
