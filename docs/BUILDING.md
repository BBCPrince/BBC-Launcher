# Building BBC Launcher

This guide explains the easiest way to build a working test package from this repository.

BBC Launcher is source-only. The scripts can help stage Minecraft files, but the repository itself must not contain Minecraft binaries, assets, Java runtimes, private keys, or account tokens.

## 1. Install Build Tools

Install these first:

- Visual Studio 2022
- Desktop development with C++
- Universal Windows Platform development
- Windows SDK 10.0.26100 or newer
- CMake
- Git
- Python 3
- WinFlexBison

You also need Xbox Dev Mode or another UWP test device where sideloaded packages can be installed.

## 2. Create Placeholder App Assets

The UWP package needs logo files. If you do not have final artwork yet, generate placeholders:

```powershell
.\scripts\New-PlaceholderAssets.ps1
```

This only creates package artwork. It does not create or download Minecraft content.

## 3. Build Mesa UWP Runtime

The native launcher needs the Mesa UWP OpenGL runtime. Build and stage it with:

```powershell
.\scripts\Build-MesaUwpRuntime.ps1 -Configuration Release
```

After this step, check that this file exists:

```text
native\mesa-uwp\opengl32.dll
```

If that file is missing, the final package build will fail because the game host cannot provide OpenGL.

## 4. Stage Minecraft Files

For a single test version:

```powershell
.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.11 -DownloadJava -IncludeAssets
```

By default this writes into:

```text
local-test-files\LocalState\
```

Then build the package:

```powershell
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release
```

## 5. Stage Multiple Versions

The launcher can be built with a multi-version payload. A clean setup looks like this:

```powershell
.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.11 -OutputRoot .\local-test-files\LocalState-1.21.11 -DownloadJava -IncludeAssets
.\scripts\New-FabricProfile.ps1 -MinecraftVersion 1.21.11 -BaseProfileRoot .\local-test-files\LocalState-1.21.11 -OutputRoot .\local-test-files\LocalState-1.21.11-Fabric

.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.1 -OutputRoot .\local-test-files\LocalState-1.21.1 -DownloadJava -IncludeAssets
.\scripts\New-FabricProfile.ps1 -MinecraftVersion 1.21.1 -BaseProfileRoot .\local-test-files\LocalState-1.21.1 -OutputRoot .\local-test-files\LocalState-1.21.1-Fabric

.\scripts\Get-MinecraftFiles.ps1 -Version 1.12.2 -OutputRoot .\local-test-files\LocalState-1.12.2 -DownloadJava -IncludeAssets
.\scripts\New-ForgeProfile.ps1 -MinecraftVersion 1.12.2 -BaseProfileRoot .\local-test-files\LocalState-1.12.2 -OutputRoot .\local-test-files\LocalState-1.12.2-Forge
```

Then combine the staged folders:

```powershell
.\scripts\New-MultiVersionPayload.ps1 -EnableAccountSignin
```

The combined payload is written to:

```text
local-test-files\LocalState-MultiVersion\
```

Build the package with that payload:

```powershell
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release -PayloadRoot .\local-test-files\LocalState-MultiVersion
```

## 6. Add Optional Mods

Put optional mods in `local-test-files\BundledMods` before running `New-MultiVersionPayload.ps1`.

Suggested folder layout:

```text
local-test-files\BundledMods\
  1.21.11\
    default\
    legacy4j\
  1.21.1\
    default\
  1.12.2\
    default\
```

Use the folders for the profile names you want the launcher to expose. Keep mod `.jar` files out of git unless they are your own files and you are allowed to redistribute them.

See [Mod distribution notes](MOD_DISTRIBUTION.md) before making any public release that includes third-party mod jars.

The launcher treats folders under each version folder as selectable mod sets. For example, `mods-library\1.21.11\default` shows up as `default`, and `mods-library\1.21.11\legacy4j` shows up as `legacy4j`. Loose `.jar` files directly under the version folder are still available as shared support mods for that version.

## 7. Account Sign-In

`New-MultiVersionPayload.ps1 -EnableAccountSignin` writes an `enable-account-signin` marker into the packaged payload. With that marker present, the native launcher asks for Microsoft sign-in on first launch and caches the Minecraft session in LocalState.

The repository does not include a Microsoft/Azure client ID. For account sign-in, provide your own app registration locally with one of these options:

```powershell
$env:MINECRAFT_XBOX_MICROSOFT_CLIENT_ID = "YOUR-CLIENT-ID"
```

or place this ignored local file in the app LocalState payload before packaging:

```text
microsoft-client-id.txt
```

Private builds can also use `microsoft-client-id.obf` instead of the text file to hide the client ID from casual package browsing. This is obfuscation only, not strong secrecy: a determined person can still recover any client ID that the app itself can use.

When account sign-in is enabled, the native launcher verifies the Microsoft/Minecraft session before hydrating profile downloads. To force that behavior for a private payload, include this ignored marker file beside the client ID file:

```text
require-account-ownership-before-download
```

Do not commit `microsoft-client-id.txt`, `microsoft-client-id.obf`, cached auth sessions, access tokens, or generated package logs. The native launcher stores Minecraft sessions in `minecraft-auth-session.protected` when sign-in succeeds.

## 8. Find the Package

Built packages are written under:

```text
artifacts\xbox\MinecraftJavaTestFixes\
```

For public GitHub release packages, build the downloader variants instead of a full local payload package:

```powershell
.\scripts\Build-PublicReleaseVariants.ps1 `
  -Configuration Release `
  -PayloadRoot .\local-test-files\LocalState-MultiVersion-320Style `
  -Target120FpsBuild
```

That creates 720p, 1080p, 1440p, and 4K MSIX files under `artifacts\xbox\PublicRelease`. The public build path uses first-launch downloads and excludes Minecraft client jars, asset objects, remap-cache jars, and bundled mod jars from the package. It still packages the Java runtimes because the current downloader manifest does not hydrate those files.

The package project name may still say `CoreWindowProbe` or `MinecraftJavaTestFixes`. That is expected for this codebase. The user-facing display name is BBC Launcher.

## 9. Install the Test Certificate

The build script creates a test signing certificate under:

```text
local-test-files\certs\
```

Install and trust the `.cer` file on the target device before installing the `.msix`.

If the device reports that the package is not signed or the signature cannot be validated, the certificate is not trusted on that device or the package was copied without the matching certificate.

## 10. Read Logs

Runtime logs are written in the app LocalState folder:

```text
logs\native-corewindow-probe.log
logs\native-glfw-from-native-host.log
logs\native-minecraft.log
```

When debugging, collect all three logs from the same launch. One log usually shows the native host state, one shows GLFW/input/window state, and one shows the Minecraft/JVM side.

## Build Paths in This Repo

There are two main package paths:

- `scripts\Build-MinecraftJavaTestFixes.ps1` builds the current working native CoreWindow package.
- `scripts\Build-XboxPackage.ps1` builds the older C# UWP launcher shell.

For a working game launch, start with `Build-MinecraftJavaTestFixes.ps1`.
