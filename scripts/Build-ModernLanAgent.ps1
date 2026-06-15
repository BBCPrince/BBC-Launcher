param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$agentRoot = Join-Path $repoRoot "native\modern-lan-agent"
$srcRoot = Join-Path $agentRoot "src"
$buildRoot = Join-Path $agentRoot "build"
$classesDir = Join-Path $buildRoot "classes"
$jarRoot = Join-Path $buildRoot "jar"
$toolsDir = Join-Path $repoRoot "local-test-files\tools"
$asmJar = Join-Path $toolsDir "asm-9.9.jar"
$manifest = Join-Path $buildRoot "MANIFEST.MF"
$outJar = Join-Path $repoRoot "native\xbox-modern-lan-agent.jar"

New-Item -ItemType Directory -Force -Path $toolsDir, $classesDir, $jarRoot | Out-Null

if (-not (Test-Path $asmJar)) {
    $asmUrl = "https://repo1.maven.org/maven2/org/ow2/asm/asm/9.9/asm-9.9.jar"
    Write-Host "Downloading ASM 9 bytecode library..."
    Invoke-WebRequest -Uri $asmUrl -OutFile $asmJar
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

$javacArgs = @("--release", "8", "-cp", $asmJar, "-d", $classesDir) + $sources
& $javac @javacArgs
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

Copy-Item -Path (Join-Path $classesDir "*") -Destination $jarRoot -Recurse -Force
Push-Location $jarRoot
try {
    & $jar xf $asmJar
} finally {
    Pop-Location
}

@"
Manifest-Version: 1.0
Premain-Class: com.minecraftxbox.lan.ModernLanAgent

"@ | Set-Content -Path $manifest -Encoding ASCII

& $jar cfm $outJar $manifest -C $jarRoot .
if ($LASTEXITCODE -ne 0) {
    throw "jar failed with exit code $LASTEXITCODE"
}

Write-Host "Modern LAN patch agent:"
Write-Host "  $outJar"
