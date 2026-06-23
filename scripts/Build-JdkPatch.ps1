[CmdletBinding()]
param(
    [string]$JavacPath
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$patchRoot = Join-Path $root "native\jdk-patch"
$src = Join-Path $patchRoot "src"
$nativeOut = Join-Path $root "native"

if (-not (Test-Path $src)) {
    throw "jdk-patch source directory not found: $src"
}

function Get-Javac {
    param([string]$Override)
    if ($Override -and (Test-Path $Override)) { return $Override }

    if ($env:JAVA_HOME) {
        $candidate = Join-Path $env:JAVA_HOME "bin\javac.exe"
        if (Test-Path $candidate) { return $candidate }
    }

    $cmd = Get-Command javac -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $rootPaths = @(
        "C:\Program Files\Eclipse Adoptium",
        "C:\Program Files\Microsoft",
        "C:\Program Files\Java",
        "C:\Program Files\Zulu",
        "C:\Program Files\Amazon Corretto"
    )
    foreach ($root in $rootPaths) {
        if (-not (Test-Path $root)) { continue }
        $found = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName "bin\javac.exe") } |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($found) {
            return (Join-Path $found.FullName "bin\javac.exe")
        }
    }

    throw "javac not found. Install JDK 11+ or set JAVA_HOME / pass -JavacPath."
}

$javac = Get-Javac $JavacPath
$jar = Join-Path (Split-Path $javac) "jar.exe"
if (-not (Test-Path $jar)) {
    throw "jar.exe not found next to $javac"
}

Write-Host "==> javac: $javac"
Write-Host "==> jar  : $jar"

$probeSrc = Join-Path $src "XboxPathProbe.java"
$probeOutDir = Join-Path $patchRoot "out-probe"
if (Test-Path $probeOutDir) { Remove-Item $probeOutDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $probeOutDir | Out-Null
Write-Host "==> Compiling XboxPathProbe.java"
& $javac -d $probeOutDir $probeSrc | Out-Host
if ($LASTEXITCODE -ne 0) { throw "javac (probe) failed (exit $LASTEXITCODE)" }

$probeJar = Join-Path $nativeOut "XboxPathProbe.jar"
if (Test-Path $probeJar) { Remove-Item $probeJar -Force }
$manifest = Join-Path $probeOutDir "manifest.mf"
Set-Content -Path $manifest -Value "Manifest-Version: 1.0`r`nMain-Class: XboxPathProbe`r`n" -Encoding ASCII
Write-Host "==> Packing XboxPathProbe.jar"
Push-Location $probeOutDir
try {
    & $jar -cfm $probeJar manifest.mf (Get-ChildItem -Filter "XboxPathProbe*.class" | Select-Object -ExpandProperty Name) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "jar (probe) failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "    OK: native\XboxPathProbe.jar"

$patchSrc = @(
    (Join-Path $src "sun\nio\fs\WindowsLinkSupport.java"),
    (Join-Path $src "java\io\WinNTFileSystem.java")
)
$patchOutDir = Join-Path $patchRoot "out-patch"
if (Test-Path $patchOutDir) { Remove-Item $patchOutDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $patchOutDir | Out-Null
Write-Host "==> Compiling java.base patch classes"
& $javac `
    --patch-module "java.base=$src" `
    -d $patchOutDir `
    @patchSrc | Out-Host
if ($LASTEXITCODE -ne 0) { throw "javac (jdk patch) failed (exit $LASTEXITCODE)" }

$jarPath = Join-Path $nativeOut "xbox-jdk-patch.jar"
if (Test-Path $jarPath) { Remove-Item $jarPath -Force }
Write-Host "==> Packing xbox-jdk-patch.jar"
Push-Location $patchOutDir
try {
    & $jar -cf $jarPath "sun" "java" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "jar failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "    OK: native\xbox-jdk-patch.jar"

$linkJarPath = Join-Path $nativeOut "xbox-jdk-link-patch.jar"
if (Test-Path $linkJarPath) { Remove-Item $linkJarPath -Force }
Write-Host "==> Packing xbox-jdk-link-patch.jar"
Push-Location $patchOutDir
try {
    & $jar -cf $linkJarPath "sun" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "jar (link patch) failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "    OK: native\xbox-jdk-link-patch.jar"

Write-Host ""
Write-Host "Done. Artifacts in $nativeOut :"
Get-ChildItem $nativeOut -File | Where-Object { $_.Name -match "XboxPathProbe|xbox-jdk-patch|xbox-jdk-link-patch" } | ForEach-Object {
    "    {0}  {1:N0} bytes" -f $_.Name, $_.Length
}
