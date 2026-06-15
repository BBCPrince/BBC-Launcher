# Creates minimal placeholder PNGs for UWP packaging.
$assetDir = Join-Path $PSScriptRoot "..\src\BBCLauncher\Assets"
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null

Add-Type -AssemblyName System.Drawing
function New-Png($path, $width, $height, $color) {
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear($color)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
}

$dark = [System.Drawing.Color]::FromArgb(255, 30, 30, 30)
New-Png (Join-Path $assetDir "StoreLogo.png") 50 50 $dark
New-Png (Join-Path $assetDir "Square44x44Logo.png") 44 44 $dark
New-Png (Join-Path $assetDir "Square150x150Logo.png") 150 150 $dark
New-Png (Join-Path $assetDir "Square480x480Logo.png") 480 480 $dark
New-Png (Join-Path $assetDir "Wide310x150Logo.png") 310 150 $dark
New-Png (Join-Path $assetDir "SplashScreen.png") 620 300 $dark
Write-Host "Assets written to $assetDir"
