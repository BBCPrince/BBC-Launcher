[CmdletBinding()]
param(
    [string]$MesaUwpRepo = "https://github.com/aerisarn/Mesa-UWP.git",
    [string]$MesaUwpRef = "main",
    [string]$MesonRepo = "https://github.com/aerisarn/meson.git",
    [string]$MesonRef = "master",
    [string]$SdlUwpGlRepo = "https://github.com/aerisarn/SDL-uwp-gl.git",
    [string]$SdlUwpGlRef = "release-2.28.5-uwp-gl",
    [switch]$SkipSdl
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$externalRoot = Join-Path $repoRoot "external"
New-Item -ItemType Directory -Force -Path $externalRoot | Out-Null

function Invoke-GitChecked {
    param(
        [Parameter(Mandatory)] [string[]]$Arguments
    )

    & git @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Sync-Repository {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string]$Repo,
        [Parameter(Mandatory)] [string]$Ref
    )

    $target = Join-Path $externalRoot $Name
    if (Test-Path $target) {
        if (-not (Test-Path (Join-Path $target ".git"))) {
            throw "$target exists but is not a git checkout."
        }

        Write-Host "Updating $Name"
        Invoke-GitChecked @("-C", $target, "fetch", "--all", "--tags", "--prune")
    }
    else {
        Write-Host "Cloning $Name"
        Invoke-GitChecked @("clone", $Repo, $target)
    }

    Invoke-GitChecked @("-C", $target, "checkout", $Ref)
    $commit = & git -C $target rev-parse HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to read commit for $Name"
    }

    Write-Host ("{0}: {1}" -f $Name, $commit)
}

Sync-Repository -Name "mesa-uwp" -Repo $MesaUwpRepo -Ref $MesaUwpRef
Sync-Repository -Name "meson" -Repo $MesonRepo -Ref $MesonRef
if (-not $SkipSdl) {
    Sync-Repository -Name "sdl-uwp-gl" -Repo $SdlUwpGlRepo -Ref $SdlUwpGlRef
}

Write-Host ""
Write-Host "Mesa-UWP source is ready under:"
Write-Host "  $externalRoot"
