# obs-dlss5-nr

**Unofficial DLSS 5 Neural Rendering filter for OBS Studio** — runs NVIDIA DLSS 5 NR (NGX feature 18) on any video source (Game Capture, Media Source, Webcam, …) with a **Default / Natural / Cinematic** style selector.

> ⚠️ **Windows x64 + NVIDIA RTX only.** Linux/macOS builds compile as no-ops.
> This project is not affiliated with, endorsed by, or supported by NVIDIA or the OBS Project. It targets an undocumented, pre-release interface; behavior may change with NVIDIA driver or `nvngx_dlssnr.dll` versions.

## Status — Phase 4

- [x] Plugin skeleton from obs-plugintemplate, renamed `obs-dlss5-nr`
- [x] Full properties UI: Style (Default/Natural/Cinematic), Render Preset, Intensity, Local Tone, Local Structure, Skin, Auto Mask, UI Correction, GPU Index, Channel Order, Reset History
- [x] Release packaging + CI (automated Windows builds and draft releases on tags)
- [x] D3D12 + NGX bridge: feature 18 processing with fail-open pass-through on any error
- [ ] **Phase 5** — zero-copy D3D11↔D3D12 interop, optical-flow motion vectors

### Known limitations

- NR runs synchronously on the OBS graphics thread — expect reduced fps while the filter is active.
- One NR filter at a time (the NGX feature is shared; multiple instances thrash rebuilds).
- Style/Preset changes rebuild the NGX feature (one-frame hitch). Other sliders apply live.
- Frame processing uses CPU staging; a zero-copy GPU interop path is planned.

## Requirements

- Windows 10/11 x64
- NVIDIA RTX GPU (see compatibility below) with driver **616.56 or newer**
- OBS Studio 31.1 or newer
- A **legally obtained** `nvngx_dlssnr.dll` — **this project never redistributes NVIDIA binaries**

## Installation

1. Download `obs-dlss5-nr-<version>-windows-x64.zip` from [Releases](https://github.com/Saganaki22/obs-dlss5-nr/releases).
2. Extract it into `%APPDATA%\obs-studio\plugins\` so you end up with:

   ```
   %APPDATA%\obs-studio\plugins\obs-dlss5-nr\bin\64bit\obs-dlss5-nr.dll
   %APPDATA%\obs-studio\plugins\obs-dlss5-nr\bin\64bit\nvngx.dll_obs.dll
   %APPDATA%\obs-studio\plugins\obs-dlss5-nr\data\locale\en-US.ini
   ```

3. Place your `nvngx_dlssnr.dll` in the plugin's config folder:

   ```
   %APPDATA%\obs-studio\plugin_config\obs-dlss5-nr\runtime\nvngx_dlssnr.dll
   ```

   The filter's **Status** line in the properties panel shows this exact path if the runtime is missing.

4. Restart OBS, add the filter to any video source:
   *Filters → Effect Filters → DLSS 5 Neural Rendering*

The NGX core (`_nvngx.dll`) is not copied by hand — the plugin discovers it automatically in your installed NVIDIA driver.

## GPU / runtime compatibility

DLSS NR support is decided by the `nvngx_dlssnr.dll` build you supply — the plugin relays the runtime's verdict to the Status line and falls back to clean pass-through video when a GPU is rejected.

| GPU | Status |
|---|---|
| RTX 50xx | Expected to work with runtime builds targeting DLSS 5 |
| RTX 40xx | Works with runtime builds patched for Ada |
| RTX 30xx | Unverified — the runtime may reject it |
| Older / non-RTX / AMD / Intel | Not supported |

If the Status line shows `0xBAD00001`, your runtime build does not support your GPU/driver combination.

## Building from source

Same toolchain as [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate): Visual Studio 2022 (or Build Tools), CMake ≥ 3.28, Windows SDK 10.0.22621+.

```powershell
cmake -S . -B build_x64
cmake --build build_x64 --config RelWithDebInfo
```

Non-Windows platforms build an empty module (the filter only registers on `_WIN32`).

## Releases

Tags using semantic versioning (e.g. `0.3.0`) trigger the GitHub Actions release workflow, which builds Windows x64 packages and creates a draft release. See `docs/RELEASING.md`.

## License

GPL-2.0 (see `LICENSE`). Integration follows the NVIDIA NGX / DLSS SDK interface — see `THIRD_PARTY_NOTICES.md`.
