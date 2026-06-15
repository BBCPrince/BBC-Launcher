# Third-Party Notices

This project can build and stage experimental OpenGL-to-D3D12 runtime pieces from these upstream projects:

- Mesa-UWP: https://github.com/aerisarn/Mesa-UWP
  - License: Mesa MIT-style license, with component licenses documented by upstream Mesa.
  - Current local fetch path: `external/mesa-uwp`.
- Meson UWP fork: https://github.com/aerisarn/meson
  - License: Apache-2.0, as documented by upstream Meson.
  - Current local fetch path: `external/meson`.
- SDL-uwp-gl: https://github.com/aerisarn/SDL-uwp-gl
  - License: zlib.
  - Current local fetch path: `external/sdl-uwp-gl`.

The `external/`, `.tools/`, and `artifacts/` directories are intentionally ignored by git. They are build inputs and outputs, not clean-room launcher source. Do not copy GPL game-port code into this repository unless the whole distribution plan is changed to comply with that license.
