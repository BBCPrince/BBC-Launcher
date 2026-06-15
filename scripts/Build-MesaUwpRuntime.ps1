[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipFetch,
    [switch]$ConfigureOnly,
    [switch]$NoWipe,
    [switch]$CleanStage
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$externalRoot = Join-Path $repoRoot "external"
$mesaRoot = Join-Path $externalRoot "mesa-uwp"
$mesonScript = Join-Path $externalRoot "meson\meson.py"
$toolsRoot = Join-Path $repoRoot ".tools"
$venvRoot = Join-Path $toolsRoot "mesa-uwp-venv"
$stageRoot = Join-Path $repoRoot "native\mesa-uwp"
$buildName = if ($Configuration -eq "Release") { "build_uwp_release" } else { "build_uwp_debug" }
$buildRoot = Join-Path $mesaRoot $buildName

function Assert-PathUnderRoot {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Root
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside $fullRoot : $fullPath"
    }
}

function Get-VsInstallPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2022."
    }

    $installPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "Visual Studio 2022 C++ tools not found. Install the Desktop C++ and UWP workloads."
    }

    return $installPath
}

function ConvertTo-CmdArgument {
    param([Parameter(Mandatory)] [string]$Argument)

    if ($Argument -match '[\s&()<>|^"]') {
        return '"' + $Argument.Replace('"', '""') + '"'
    }

    return $Argument
}

function Join-CmdArguments {
    param([Parameter(Mandatory)] [string[]]$Arguments)

    return ($Arguments | ForEach-Object { ConvertTo-CmdArgument $_ }) -join ' '
}

function Invoke-VsDevCommand {
    param(
        [Parameter(Mandatory)] [string]$CommandLine,
        [Parameter(Mandatory)] [string]$WorkingDirectory,
        [Parameter(Mandatory)] [string]$VcVarsPath
    )

    Push-Location $WorkingDirectory
    try {
        & cmd.exe /d /s /c "call `"$VcVarsPath`" >nul && $CommandLine" | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $LASTEXITCODE : $CommandLine"
        }
    }
    finally {
        Pop-Location
    }
}

function Get-Python {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return $py.Source
    }

    throw "Python was not found on PATH."
}

function Add-WinFlexBisonToPath {
    function Use-WinFlexBisonDir {
        param([string]$Directory)

        if (-not $Directory) {
            return $false
        }

        if ((Test-Path (Join-Path $Directory "win_flex.exe")) -and
            (Test-Path (Join-Path $Directory "win_bison.exe")) -and
            (Test-Path (Join-Path $Directory "data\m4sugar\m4sugar.m4"))) {
            $env:PATH = $Directory + ";" + $env:PATH
            Write-Host "Using WinFlexBison from: $Directory"
            return $true
        }

        return $false
    }

    $searchRoots = @(
        (Join-Path $env:LocalAppData "Microsoft\WinGet\Packages"),
        $env:ProgramFiles,
        ${env:ProgramFiles(x86)}
    ) | Where-Object { $_ -and (Test-Path $_) }

    foreach ($root in $searchRoots) {
        $candidate = Get-ChildItem -LiteralPath $root -Recurse -Filter "win_flex.exe" -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $candidate) {
            continue
        }

        if (Use-WinFlexBisonDir $candidate.DirectoryName) {
            return
        }
    }

    $flex = Get-Command win_flex -ErrorAction SilentlyContinue
    $bison = Get-Command win_bison -ErrorAction SilentlyContinue
    if ($flex -and $bison -and (Use-WinFlexBisonDir (Split-Path -Parent $bison.Source))) {
        return
    }

    throw "win_flex.exe and win_bison.exe were not found. Install WinFlexBison and rerun this script."
}

function Copy-IfExists {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Destination
    )

    if (Test-Path $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        Write-Host ("Copied {0}" -f (Split-Path -Leaf $Source))
        return $true
    }

    return $false
}

function Copy-UwpVclibsRuntime {
    param([Parameter(Mandatory)] [string]$Destination)

    $appxRoot = Join-Path ${env:ProgramFiles(x86)} "Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx"
    if (-not (Test-Path $appxRoot)) {
        Write-Warning "Microsoft.VCLibs Extension SDK was not found; Mesa-UWP may fail to load on Xbox without the UWP app CRT."
        return
    }

    $vclibsAppx = Get-ChildItem -LiteralPath $appxRoot -Recurse -Filter "Microsoft.VCLibs.x64.14.00.appx" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\Retail\x64\*" } |
        Select-Object -First 1
    if (-not $vclibsAppx) {
        $vclibsAppx = Get-ChildItem -LiteralPath $appxRoot -Recurse -Filter "Microsoft.VCLibs.x64.14.00.appx" -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }

    if (-not $vclibsAppx) {
        Write-Warning "Microsoft.VCLibs.x64.14.00.appx was not found; Mesa-UWP may fail to load on Xbox without the UWP app CRT."
        return
    }

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $requiredDlls = @(
        "msvcp140_app.dll",
        "vccorlib140_app.dll",
        "vcruntime140_1_app.dll",
        "vcruntime140_app.dll"
    )

    $zip = [System.IO.Compression.ZipFile]::OpenRead($vclibsAppx.FullName)
    try {
        foreach ($dllName in $requiredDlls) {
            $entry = $zip.Entries |
                Where-Object { $_.Name -ieq $dllName } |
                Select-Object -First 1
            if (-not $entry) {
                Write-Warning "UWP app CRT DLL missing from $($vclibsAppx.FullName): $dllName"
                continue
            }

            [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
                $entry,
                (Join-Path $Destination $entry.Name),
                $true)
            Write-Host ("Copied {0} from Microsoft.VCLibs appx" -f $entry.Name)
        }
    }
    finally {
        $zip.Dispose()
    }
}

if (-not $SkipFetch) {
    & (Join-Path $PSScriptRoot "Get-MesaUwp.ps1") | Out-Host
}

if (-not (Test-Path $mesaRoot)) {
    throw "Mesa-UWP checkout missing: $mesaRoot. Run scripts\Get-MesaUwp.ps1 first."
}

if (-not (Test-Path $mesonScript)) {
    throw "Mesa-UWP Meson fork missing: $mesonScript. Run scripts\Get-MesaUwp.ps1 first."
}

New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
$python = Get-Python
if (-not (Test-Path (Join-Path $venvRoot "Scripts\python.exe"))) {
    Write-Host "Creating Python venv for Mesa-UWP build tools"
    & $python -m venv $venvRoot | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "python -m venv failed with exit code $LASTEXITCODE"
    }
}

$venvPython = Join-Path $venvRoot "Scripts\python.exe"
& $venvPython -m pip install --upgrade pip mako packaging pyyaml | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install Python build dependencies."
}

$venvScripts = Split-Path -Parent $venvPython
$venvPython3 = Join-Path $venvScripts "python3.exe"
if (-not (Test-Path $venvPython3)) {
    Copy-Item -LiteralPath $venvPython -Destination $venvPython3 -Force
}
$env:PATH = $venvScripts + ";" + $env:PATH
$env:PYTHON = $venvPython

Add-WinFlexBisonToPath

$vsInstall = Get-VsInstallPath
$vcVars = Join-Path $vsInstall "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcVars)) {
    throw "vcvars64.bat not found: $vcVars"
}

if ((Test-Path $buildRoot) -and $NoWipe) {
    Write-Host "Using existing Mesa-UWP configure: $buildRoot"
}
else {
    $buildType = if ($Configuration -eq "Release") { "release" } else { "debug" }
    $setupArgs = @(
        $venvPython,
        $mesonScript,
        "setup",
        $buildName,
        "--backend=vs",
        "--buildtype=$buildType",
        "--uwp",
        "-Dcpp_std=vc++17",
        "-Dcpp_args=['/D _XBOX_UWP','/D _XBOX_FMALLOC','/wd4189']",
        "-Dc_args=['/D _XBOX_UWP','/D _XBOX_FMALLOC','/wd4189']",
        "-Db_pch=false",
        "-Dc_winlibs=[]",
        "-Dcpp_winlibs=[]",
        "-Dgallium-drivers=d3d12,swrast",
        "-Dvulkan-drivers=[]",
        "-Dtools=[]",
        "-Dbuild-tests=false",
        "-Dgallium-d3d12-video=disabled",
        "-Dxmlconfig=disabled",
        "-Dshader-cache=disabled",
        "-Dzlib=disabled",
        "-Dzstd=disabled"
    )

    if (Test-Path $buildRoot) {
        $setupArgs = $setupArgs[0..3] + @("--wipe") + $setupArgs[4..($setupArgs.Count - 1)]
    }

    Write-Host "Configuring Mesa-UWP ($Configuration)"
    Invoke-VsDevCommand -CommandLine (Join-CmdArguments $setupArgs) -WorkingDirectory $mesaRoot -VcVarsPath $vcVars
}

if ($ConfigureOnly) {
    Write-Host "ConfigureOnly requested; skipping compile and staging."
    return
}

$compileArgs = @($venvPython, $mesonScript, "compile", "-C", $buildName, "--jobs", "1")
Write-Host "Building Mesa-UWP ($Configuration)"
try {
    Invoke-VsDevCommand -CommandLine (Join-CmdArguments $compileArgs) -WorkingDirectory $mesaRoot -VcVarsPath $vcVars
}
catch {
    Write-Warning "Mesa-UWP compile failed once; retrying because Visual Studio custom parser targets can complete on a second pass."
    Invoke-VsDevCommand -CommandLine (Join-CmdArguments $compileArgs) -WorkingDirectory $mesaRoot -VcVarsPath $vcVars
}

if ($CleanStage -and (Test-Path $stageRoot)) {
    Assert-PathUnderRoot -Path $stageRoot -Root (Join-Path $repoRoot "native")
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

$openGlDll = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "opengl32.dll" -File -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notlike "*\meson-private\*" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $openGlDll) {
    throw "Mesa-UWP build completed but opengl32.dll was not found under $buildRoot"
}

$sourceDirs = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::OrdinalIgnoreCase)
[void]$sourceDirs.Add($openGlDll.DirectoryName)

$galliumDll = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "libgallium_wgl.dll" -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($galliumDll) {
    [void]$sourceDirs.Add($galliumDll.DirectoryName)
}

foreach ($sourceDir in $sourceDirs) {
    Get-ChildItem -LiteralPath $sourceDir -Filter "*.dll" -File |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stageRoot $_.Name) -Force
            Write-Host ("Staged {0}" -f $_.Name)
        }
}

foreach ($dllName in @("libglapi.dll", "xbox_fmalloc.dll", "libEGL.dll", "libGLESv1_CM.dll", "libGLESv2.dll")) {
    $dll = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter $dllName -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($dll) {
        Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $stageRoot $dll.Name) -Force
        Write-Host ("Staged {0}" -f $dll.Name)
    }
    elseif ($dllName -eq "libglapi.dll" -or $dllName -eq "xbox_fmalloc.dll") {
        throw "Required Mesa-UWP runtime DLL was not found: $dllName"
    }
}

Copy-IfExists -Source (Join-Path $repoRoot "native\Ole32.dll") -Destination $stageRoot | Out-Null
Copy-UwpVclibsRuntime -Destination $stageRoot

$dxilTarget = Join-Path $stageRoot "dxil.dll"
& (Join-Path $repoRoot "scripts\Get-DxilRuntime.ps1") -TargetPath $dxilTarget

$crtRoots = Get-ChildItem -LiteralPath (Join-Path $vsInstall "VC\Redist\MSVC") -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending
foreach ($crtRoot in $crtRoots) {
    $crtDir = Join-Path $crtRoot.FullName "x64\Microsoft.VC143.CRT"
    if (-not (Test-Path $crtDir)) {
        continue
    }

    Copy-IfExists -Source (Join-Path $crtDir "msvcp140.dll") -Destination $stageRoot | Out-Null
    Copy-IfExists -Source (Join-Path $crtDir "vcruntime140.dll") -Destination $stageRoot | Out-Null
    Copy-IfExists -Source (Join-Path $crtDir "vcruntime140_1.dll") -Destination $stageRoot | Out-Null
    break
}

$licenseDir = Join-Path $stageRoot "licenses"
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
Copy-IfExists -Source (Join-Path $mesaRoot "docs\license.rst") -Destination (Join-Path $licenseDir "Mesa-license.rst") | Out-Null
Copy-IfExists -Source (Join-Path $externalRoot "sdl-uwp-gl\LICENSE.txt") -Destination (Join-Path $licenseDir "SDL-uwp-gl-LICENSE.txt") | Out-Null

$mesaCommit = & git -C $mesaRoot rev-parse HEAD
$mesonCommit = & git -C (Join-Path $externalRoot "meson") rev-parse HEAD
@(
    "Mesa-UWP runtime staged by scripts\Build-MesaUwpRuntime.ps1",
    "Mesa-UWP: https://github.com/aerisarn/Mesa-UWP $mesaCommit",
    "Meson fork: https://github.com/aerisarn/meson $mesonCommit",
    "Configuration: $Configuration",
    "Build directory: $buildRoot"
) | Set-Content -Path (Join-Path $stageRoot "BUILD-SOURCE.txt") -Encoding ASCII

Write-Host ""
Write-Host "Mesa-UWP runtime staged:"
Write-Host "  $stageRoot"
Write-Host "OpenGL entry DLL:"
Write-Host "  $(Join-Path $stageRoot "opengl32.dll")"
