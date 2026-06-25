# BBC Launcher

BBC Launcher, short for Big Block Craft Launcher, is an experimental UWP/Xbox launcher and native host for running Minecraft Java Edition on Xbox-style UWP targets.

This repository is source-only. It does not include Minecraft game files, Mojang assets, Java runtimes, Microsoft account secrets, private signing keys, or paid content. Each builder must download or provide their own legal Minecraft files.

Parts of this project were developed with AI assistance from OpenAI ChatGPT/Codex and then tested and edited by the maintainer.

## Current Status

The recommended working path is the native CoreWindow package in `native/corewindow-probe`, built through:

```powershell
.\scripts\Build-MinecraftJavaTestFixes.ps1
```

That package is the path that currently launches the game. The older C# UWP shell under `src\BBCLauncher` is still useful project history and launcher UI work, but it is not the main working game host.

## What You Need

- Windows 10 or Windows 11
- Visual Studio 2022 with C++ UWP tools
- Windows SDK 10.0.26100 or newer
- Git
- CMake
- Python 3
- Xbox Dev Mode or another UWP test device
- A legal Minecraft Java account and game files

Mesa UWP runtime files are also required. The native package expects them under:

```text
native\mesa-uwp\
```

The build script will stop if `native\mesa-uwp\opengl32.dll` is missing.

## Fast Build Path

Run these commands from the repository root:

```powershell
cd C:\Users\coolg\projects\BBC-Launcher

.\scripts\New-PlaceholderAssets.ps1
.\scripts\Build-MesaUwpRuntime.ps1 -Configuration Release

.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.11 -DownloadJava -IncludeAssets
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release
```

The generated package is written under:

```text
artifacts\xbox\MinecraftJavaTestFixes\
```

The script also creates a local test signing certificate under:

```text
local-test-files\certs\
```

Install and trust that certificate on the test device before installing the `.msix`.

## Multi-Version Payload

To prepare a payload with multiple Minecraft versions, stage each version first, then build a combined payload:

```powershell
.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.11 -OutputRoot .\local-test-files\LocalState-1.21.11 -DownloadJava -IncludeAssets
.\scripts\New-FabricProfile.ps1 -MinecraftVersion 1.21.11 -BaseProfileRoot .\local-test-files\LocalState-1.21.11 -OutputRoot .\local-test-files\LocalState-1.21.11-Fabric

.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.1 -OutputRoot .\local-test-files\LocalState-1.21.1 -DownloadJava -IncludeAssets
.\scripts\New-FabricProfile.ps1 -MinecraftVersion 1.21.1 -BaseProfileRoot .\local-test-files\LocalState-1.21.1 -OutputRoot .\local-test-files\LocalState-1.21.1-Fabric

.\scripts\Get-MinecraftFiles.ps1 -Version 1.12.2 -OutputRoot .\local-test-files\LocalState-1.12.2 -DownloadJava -IncludeAssets
.\scripts\New-ForgeProfile.ps1 -MinecraftVersion 1.12.2 -BaseProfileRoot .\local-test-files\LocalState-1.12.2 -OutputRoot .\local-test-files\LocalState-1.12.2-Forge

.\scripts\New-MultiVersionPayload.ps1
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release -PayloadRoot .\local-test-files\LocalState-MultiVersion
```

The combined payload is created at:

```text
local-test-files\LocalState-MultiVersion\
```

## Useful Docs

- [Build guide](docs/BUILDING.md)
- [Mod distribution notes](docs/MOD_DISTRIBUTION.md)
- [Current local test mod list and upstream links](docs/MOD_DISTRIBUTION.md#current-local-test-mod-links)
- [Project map](docs/PROJECT_MAP.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## Important Notes

- Do not commit `local-test-files`, downloaded Minecraft files, Java runtimes, signing certificates, built packages, Microsoft client IDs, account session files, access tokens, or local mod jars.
- The build output name still uses `MinecraftJavaTestFixes` in some places. That is the current native package project name. The installed app display name is BBC Launcher.
- If the app crashes or shows a black screen, check the logs in the app LocalState folder first:

```text
logs\native-corewindow-probe.log
logs\native-glfw-from-native-host.log
logs\native-minecraft.log
```
