param(
    [Parameter(Mandatory)]
    [string]$TargetPath,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if ((Test-Path $TargetPath) -and -not $Force) {
    return $TargetPath
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cacheRoot = Join-Path $repoRoot ".tools\dxc-dxil-cache"
$extractRoot = Join-Path $cacheRoot "extract"
$dxilSource = Join-Path $extractRoot "bin\x64\dxil.dll"

New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $TargetPath) | Out-Null

if (-not (Test-Path $dxilSource)) {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest" -UseBasicParsing
    $asset = $release.assets | Where-Object { $_.name -like 'dxc_*.zip' } | Select-Object -First 1
    if (-not $asset) { throw "No dxc_*.zip asset on DirectXShaderCompiler latest release." }
    $zipPath = Join-Path $cacheRoot $asset.name
    Write-Host "Downloading $($asset.name) from microsoft/DirectXShaderCompiler ($($release.tag_name))..."
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -UseBasicParsing
    if (Test-Path $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
    Expand-Archive -Path $zipPath -DestinationPath $extractRoot -Force
}

if (-not (Test-Path $dxilSource)) { throw "dxil.dll not found at $dxilSource" }
Copy-Item -LiteralPath $dxilSource -Destination $TargetPath -Force
Write-Host "Staged dxil.dll from DirectXShaderCompiler -> $TargetPath"
return $TargetPath
