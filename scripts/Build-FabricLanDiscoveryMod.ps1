param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$modRoot = Join-Path $repoRoot "native\fabric-lan-discovery"
$srcRoot = Join-Path $modRoot "src"
$resourcesRoot = Join-Path $modRoot "resources"
$buildRoot = Join-Path $modRoot "build"
$classesDir = Join-Path $buildRoot "classes"
$jarRoot = Join-Path $buildRoot "jar"
$toolsDir = Join-Path $repoRoot "local-test-files\tools"
$mixinJar = Join-Path $toolsDir "sponge-mixin-0.17.2+mixin.0.8.7.jar"
$outJar = Join-Path $repoRoot "native\xbox-lan-discovery-fabric.jar"

New-Item -ItemType Directory -Force -Path $toolsDir, $classesDir, $jarRoot | Out-Null

if (-not (Test-Path $mixinJar)) {
    $mixinUrl = "https://maven.fabricmc.net/net/fabricmc/sponge-mixin/0.17.2+mixin.0.8.7/sponge-mixin-0.17.2+mixin.0.8.7.jar"
    Write-Host "Downloading Fabric Sponge Mixin annotations..."
    Invoke-WebRequest -Uri $mixinUrl -OutFile $mixinJar
}

$javac = (Get-Command javac -ErrorAction Stop).Source
$jar = (Get-Command jar -ErrorAction Stop).Source

Remove-Item -Recurse -Force -LiteralPath $classesDir -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force -LiteralPath $jarRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $classesDir, $jarRoot | Out-Null

$sources = Get-ChildItem -Path $srcRoot -Recurse -Filter *.java | ForEach-Object { $_.FullName }
if (-not $sources) {
    throw "No Java sources found under $srcRoot"
}

$javacArgs = @("--release", "8", "-proc:none", "-cp", $mixinJar, "-d", $classesDir) + $sources
& $javac @javacArgs
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

Copy-Item -Path (Join-Path $classesDir "*") -Destination $jarRoot -Recurse -Force
Copy-Item -Path (Join-Path $resourcesRoot "*") -Destination $jarRoot -Recurse -Force

& $jar cf $outJar -C $jarRoot .
if ($LASTEXITCODE -ne 0) {
    throw "jar failed with exit code $LASTEXITCODE"
}

Write-Host "Fabric LAN discovery mod:"
Write-Host "  $outJar"
