# Troubleshooting

Use this file when a build or launch fails.

## Build Fails Because Mesa Is Missing

Symptom:

```text
Required Mesa runtime file not found: native\mesa-uwp\opengl32.dll
```

Fix:

```powershell
.\scripts\Build-MesaUwpRuntime.ps1 -Configuration Release
```

Then check that `native\mesa-uwp\opengl32.dll` exists.

## Package Install Says It Is Not Signed

The package was signed with a local test certificate, but the target device does not trust that certificate yet.

Fix:

1. Open `local-test-files\certs`.
2. Find the generated `.cer` file.
3. Install/trust it on the target device.
4. Install the `.msix` again.

## App Opens But Minecraft Files Are Missing

The package was built without a valid payload or the staged payload was not copied.

Fix for a single version:

```powershell
.\scripts\Get-MinecraftFiles.ps1 -Version 1.21.11 -DownloadJava -IncludeAssets
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release
```

Fix for a multi-version payload:

```powershell
.\scripts\New-MultiVersionPayload.ps1
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release -PayloadRoot .\local-test-files\LocalState-MultiVersion
```

## Black Screen, Crash, Or No Input

Collect these logs from the same launch:

```text
logs\native-corewindow-probe.log
logs\native-glfw-from-native-host.log
logs\native-minecraft.log
```

The most useful first checks are:

- Did the CoreWindow host start?
- Did GLFW create the window?
- Did Mesa/OpenGL initialize?
- Did the JVM start?
- Did Minecraft reach the menu?
- Did the crash happen after loading resources or after entering a world?

## First Launch Crashes But Second Launch Works

This usually means first-launch staging or file extraction is incomplete when the game starts.

Try building with the full payload already staged:

```powershell
.\scripts\Build-MinecraftJavaTestFixes.ps1 -Configuration Release -PayloadRoot .\local-test-files\LocalState-MultiVersion
```

If the problem remains, compare the first-launch and second-launch `native-minecraft.log` files.

## Which Build Script Should I Use?

Use this for the current working game host:

```powershell
.\scripts\Build-MinecraftJavaTestFixes.ps1
```

Use this only when working on the older C# launcher shell:

```powershell
.\scripts\Build-XboxPackage.ps1
```
