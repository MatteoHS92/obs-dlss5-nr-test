# obs-dlss5-nr

**Unofficial DLSS 5 Neural Rendering filter for OBS Studio** — runs NVIDIA DLSS 5 NR (NGX feature 18) on any video source (Game Capture, Media Source, Webcam, …) with a **Default / Natural / Cinematic** style selector.

> ⚠️ **Windows x64 + NVIDIA RTX only.** Linux/macOS builds compile as no-ops.
> This project is not affiliated with, endorsed by, or supported by NVIDIA or the OBS Project. It targets an undocumented, pre-release interface; behavior may change with NVIDIA driver or `nvngx_dlssnr.dll` versions.

## Features (v1.1.0)

- DLSS 5 Neural Rendering on any OBS video source, live
- **Style**: Default / Natural / Cinematic (rebuilds the NR feature on change)
- **Render Preset** (0–3), **Intensity**, **Local Tone**, **Local Structure**, **Skin Structure** sliders — applied per-frame
- **Auto Mask**
- **Processing Mode**:
  - *Smooth* (default) — NR runs on a worker thread one frame behind; OBS keeps full frame rate
  - *Low latency* — inline processing for game capture where delay matters
- **NR Frame Rate** throttle — Match source / 60 / 30 / 24 / 15 fps; between NR frames the last enhanced frame is displayed (a performance dial)
- **Reset History** button (fixes smearing after scene cuts)
- Advanced: GPU Index, Channel Order (auto-detects runtime BGRA/RGBA quirks), experimental zero-copy toggle (off by default — see status)
- Fail-safe design: any NGX/runtime error falls back to clean pass-through video and is reported in the filter's Status line

## Current status

- ✅ Stable NR processing (CPU-staged) — recommended
- ⚠️ Zero-copy GPU path: experimental, **off by default**. The NR runtime currently aborts the process when writing into D3D11-shared textures at 4K. Enable only for testing.
- ❌ Motion vectors: NR runs per-frame (no temporal flow yet) — planned

## Requirements

- Windows 10/11 x64
- NVIDIA RTX GPU (see compatibility below) with driver **616.56 or newer**
- OBS Studio 31.1 or newer

## ⚠️ Required runtime file — NOT included

The release ZIP **intentionally does not contain NVIDIA's proprietary software**. You need **one file**, and you must obtain it yourself:

- **`nvngx_dlssnr.dll`** — the DLSS 5 Neural Rendering runtime (~158 MB, v310.8+)

For legal reasons this project cannot provide, link, or mirror that file. Places people legitimately acquire it:

- **Your own install of a game that ships DLSS 5 NR** — e.g. copy `nvngx_dlssnr.dll` out of the **NBA 2K27** game folder
- The **NVIDIA DLSS SDK**, if a release includes the NR runtime

Everything else is handled for you: the NGX core (`_nvngx.dll`) is discovered automatically inside your installed NVIDIA driver, and no other file from game/SDK folders is needed.

Place the file at:

```
%APPDATA%\obs-studio\plugin_config\obs-dlss5-nr\runtime\nvngx_dlssnr.dll
```

The filter's **Status** line shows this exact path when the file is missing.

## Installation

1. Download `obs-dlss5-nr-<version>-windows-x64.zip` from [Releases](https://github.com/Saganaki22/obs-dlss5-nr/releases).
2. Extract it into `C:\ProgramData\obs-studio\plugins\` so you end up with:

   ```
   C:\ProgramData\obs-studio\plugins\obs-dlss5-nr\bin\64bit\obs-dlss5-nr.dll
   C:\ProgramData\obs-studio\plugins\obs-dlss5-nr\bin\64bit\nvngx.dll_obs.dll
   C:\ProgramData\obs-studio\plugins\obs-dlss5-nr\data\locale\en-US.ini
   ```

   (This is the directory OBS scans for manually-installed plugins on Windows — not `%APPDATA%`.)

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

Tags using semantic versioning (e.g. `v1.1.0`) trigger the GitHub Actions release workflow, which builds Windows x64 packages and creates a release. See `docs/RELEASING.md`.

## License

GPL-2.0 (see `LICENSE`). Integration follows the NVIDIA NGX / DLSS SDK interface — see `THIRD_PARTY_NOTICES.md`.
