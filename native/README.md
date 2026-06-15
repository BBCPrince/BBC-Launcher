# Native runtime bridge

Minecraft Java on Xbox requires native components not fully implemented in this repo.
This folder now includes a first milestone: `graphics_bridge.dll` source with a D3D12 device probe.

| Capability | Spec requirement | Integration point |
|------------|------------------|-------------------|
| Graphics | OpenGL 3.2+ via D3D12-backed translation | `native/` DLL loaded before Java start; hook in `GraphicsProbeService` |
| Audio | Minecraft Java audio on Xbox output | Set `-Dminecraft.audio.backend` and load audio bridge DLL |
| Input | Gamepad through Minecraft-compatible controller layer | Fabric mod or JNI bridge under `native/` |
| Java bridge | JNI / process hosting inside UWP sandbox | `JavaRuntimeLauncher` + `runFullTrust` where policy allows |

Current milestone status:

- `native/graphics-bridge/presentation.cpp`: D3D12 CoreWindow swapchain + present API for Milestone 2 game host
- `mgb_create_d3d12_device_probe()`: verifies D3D12 device creation
- `mgb_presentation_ensure_swap_event()`: creates `Local\MinecraftXboxJavaPresent` for Java swap signalling
- `mgb_presentation_init(coreWindow, w, h)`: binds swapchain to launcher CoreWindow
- `mgb_presentation_present(rgba)`: clears back buffer and Present()
- `mgb_presentation_consume_swap_signal()`: polls Java `glfwSwapBuffers` events
- `mgb_get_renderer_string()`: returns renderer status text
- `mgb_get_version_string()`: bridge version string
- `mgb_get_last_error_string()`: last native error
- `native/win-shims/ole32_shim.cpp` builds **`Ole32.dll`**: redirects `oshi`/JNA hardware probe calls into safe no-op stubs so Minecraft's HW inspection cannot crash the JVM on Xbox.
- `native/win-shims/pdh_shim.cpp` builds **`Pdh.dll`**: same idea for Windows performance counters.
- `native/xbox-glfw/xbox_glfw.cpp` builds **`xbox-glfw.dll`**: stub replacement for LWJGL's bundled `glfw.dll`. Exports all 140 GLFW 3.3.x symbols with safe defaults so `glfwInit` no longer trips on `RegisterClassExW` (not supported on Xbox UWP). LWJGL is told to load it via `-Dorg.lwjgl.glfw.libname=<absolute path>`. This is **milestone 1** of three; the stub is intentionally non-rendering so we can see which GLFW entry points Mojang really needs before doing a CoreWindow-backed implementation.
- `native/xbox-opengl/xbox_opengl.cpp` builds **`xbox-opengl.dll`**: 368-export stub replacement for the system `opengl32.dll` that Xbox UWP never ships. Implements bespoke `glGetString`/`glGetIntegerv`/wgl* paths that report a fake "OpenGL 4.6 Xbox-stub" context and recognise a curated set of GL 2.0+ entry points via `wglGetProcAddress` (ID generation, shader compile status, sync objects, etc.). Selected via `-Dorg.lwjgl.opengl.libname=<absolute path>`. In **1.0.11.0** the integer-query table was extended with non-zero alignment caps (`GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`, `GL_*_STORAGE_BUFFER_OFFSET_ALIGNMENT`, `GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT`, `GL_MIN_MAP_BUFFER_ALIGNMENT`) after Mojang's UBO allocator floor-div'd by zero on 1.0.10.0.

The shims load successfully (`Native.load("Ole32")`/`Native.load("Pdh")`) and return failure codes for HW queries. `oshi` swallows these and continues without hardware detail. This bypasses the original crash:

```
java.lang.UnsatisfiedLinkError: Unable to load library 'Ole32'
java.lang.UnsatisfiedLinkError: Unable to load library 'Pdh'
```

Build:

```powershell
cd C:\Users\coolg\projects\BBC-Launcher
.\scripts\Build-NativeBridge.ps1 -Configuration Release
```

This builds and copies the native DLLs into `native\`:

- `graphics_bridge.dll`
- `Ole32.dll` (Xbox shim - do NOT deploy on desktop Windows)
- `Pdh.dll` (Xbox shim - do NOT deploy on desktop Windows)
- `xbox-glfw.dll` (Xbox GLFW stub - milestone 1; selected via `org.lwjgl.glfw.libname`)
- `xbox-opengl.dll` (Xbox OpenGL stub - selected via `org.lwjgl.opengl.libname`)
- `xbox-openal.dll` (Xbox OpenAL stub - selected via `org.lwjgl.openal.libname`)

GLFW milestone plan:

1. **Stub** *(done)*: returns safe values so LWJGL clears `glfwInit`.
2. **Presentation host** *(1.0.14.0)*: launcher navigates to `GameHostPage`, owns CoreWindow D3D12 swapchain, and presents a pulsing clear colour. `xbox-glfw.dll` signals `Local\MinecraftXboxJavaPresent` on `glfwSwapBuffers` so the host flashes green when Java's render loop is alive. Still no real Minecraft pixels - OpenGL draws remain no-ops in `xbox-opengl.dll`.
3. **CoreWindow-backed JVM** *(blocked on architecture)*: embed JVM via JNI so GLFW can read real framebuffer size/input from the same process.
4. **Swapchain integration**: pipe stub OpenGL draws into D3D12 (ANGLE/Zink) for actual game rendering.

The launcher project includes them as MSIX `Content`, so they end up in the app package under `native\`. At launch the `RuntimeAssetStager` copies them to `LocalState\native\` and the JVM is told to look there via `-Djna.library.path`, `-Djna.boot.library.path`, and `-Djava.library.path`.

Typical approaches for full OpenGL compatibility (choose one stack and test on hardware):

- ANGLE or Zink for OpenGL on D3D12
- LWJGL platform JARs rebuilt for Windows UWP/x64
- Controller: existing Fabric controller mods referenced by the spec

Mesa-UWP pilot path:

- Run `scripts\Get-MesaUwp.ps1` to fetch the Mesa-UWP source, matching Meson fork, and SDL UWP GL upstream project.
- Run `scripts\Build-MesaUwpRuntime.ps1 -Configuration Release` to configure, build, and stage the runtime under `native\mesa-uwp\`.
- The launcher expects the OpenGL entry DLL at `native\mesa-uwp\opengl32.dll`.
- Any helper DLLs Mesa needs, such as Gallium runtime DLLs, should live in the same folder.
- `launcher.config.json` uses `OpenGlProvider: "auto"`, so Mesa-UWP is selected when that DLL is present and the current `xbox-opengl.dll` shim is used otherwise.
- When Mesa-UWP is selected, the Java process gets `GALLIUM_DRIVER=d3d12`, `MESA_LOADER_DRIVER_OVERRIDE=d3d12`, and `PATH` is prefixed with the staged `mesa-uwp` folder.

Legacy desktop GLon12 notes:

- Desktop Mesa/GLon12 can still be tested explicitly with `OpenGlProvider: "glon12"` and a runtime under `native\glon12\`.
- Do not rely on desktop GLon12 for Xbox UWP testing. The WGL/GDI build imports desktop APIs and has already failed to load in the sandbox.

Place compiled `.dll` files in the app local `native/` folder at runtime.

Important: this bridge currently performs D3D12 probing only. It does not yet implement OpenGL API translation.
