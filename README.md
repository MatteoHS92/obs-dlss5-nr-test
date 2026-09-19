# obs-dlss5-nr

**Unofficial DLSS 5 Neural Rendering filter for OBS Studio** — runs NVIDIA DLSS 5 NR (NGX feature 18) on any video source (Game Capture, Media Source, Webcam, …) with a **Default / Natural / Cinematic** style selector.

> ⚠️ **Windows x64 + NVIDIA RTX only.** Linux/macOS builds compile as no-ops.
> This project is not affiliated with, endorsed by, or supported by NVIDIA or the OBS Project. It targets an undocumented, pre-release interface; behavior may change with NVIDIA driver or `nvngx_dlssnr.dll` versions.

## Features (v1.2.5)

- DLSS 5 Neural Rendering on any OBS video source, live
- **Style**: Default / Natural / Cinematic (rebuilds the NR feature on change)
- **Temporal Mode** — estimates per-frame motion vectors with NVIDIA Optical Flow (NVOFA, driver-provided) and feeds them to DLSS NR for better temporal stability and less ghosting on movement (CPU staging path only)
- **Render Preset** (0–3), **Intensity**, **Local Tone**, **Local Structure**, **Skin Structure** sliders — applied per-frame
- **Auto Mask**
- **Processing Mode**:
  - *Smooth* (default) — NR runs on a worker thread; completed frames repeat until a new result is ready. Latency and enhanced FPS depend on processing speed.
  - *Low latency* — inline processing; waits block the OBS render thread.
- **Processing Resolution** dropdown — Source / 2160p / 1440p / **1080p (default)** / 720p. Resizes on the GPU before NR, preserving aspect ratio and the original displayed source size. Smaller sources are not enlarged.
- **Live performance status** — actual enhanced FPS, processing dimensions, path, and last processing time in the Status field and periodic OBS logs. Reopen filter properties to refresh the Status snapshot.
- **NR Frame Rate** throttle — Match source / 60 / 30 / 24 / 15 fps in both processing modes; between NR frames the last enhanced frame is displayed (a performance dial)
- **Reset History** button (fixes smearing after scene cuts)
- Clean shutdown — the NGX modules are left mapped for process lifetime (unloading them deadlocked inside the NVIDIA D3D12 driver); OBS exits normally
- Advanced: GPU Index, Channel Order (auto-detects runtime BGRA/RGBA quirks), experimental zero-copy toggle (off by default — see status)
- Fail-safe design: any NGX/runtime error falls back to clean pass-through video and is reported in the filter's Status line

## Faster processing in 1.2.5

Start with **Smooth + 1080p + Temporal off**. For Temporal Mode, try **Smooth + 720p**. Existing mode and Temporal choices are retained when upgrading; the new processing-resolution default is 1080p.

The filter works at the source's dimensions before OBS's output scaling. A 4K camera can therefore cost 4K processing even with a 1080p stream. The new dropdown reduces the frame *before* CPU staging and neural rendering; choosing 1080p for a 4K source cuts the processed pixel count by 75%. Lower resolutions trade fine detail for speed.

Smooth mode's readiness check no longer waits on the worker's processing mutex. The frame limiter preserves its timing schedule through small clock variations, avoiding the old 30-to-15 FPS drop. Duplicate renders reuse completed output, and changing mode/resolution clears stale frames. Smooth mode still uses CPU staging; the experimental GPU-sharing path is only used in Low latency mode.

**OBS FPS is not enhanced FPS.** OBS may output 30 frames/sec while repeating slower NR results. Check the new enhanced-FPS status for the effective update rate.

Measured with the real OBS renderer and NR runtime on an RTX 5090, using an animated 4K synthetic source at 30 FPS, after five seconds of warm-up:

| Processing settings | Enhanced FPS | Missed OBS render frames in 10 seconds |
| --- | ---: | ---: |
| Smooth, 1080p, Temporal off | 30.0 | 0 |
| Smooth, 1080p, Temporal on | 20.9 | 0 |
| Smooth, 720p, Temporal on | 30.0 | 0 |

These measurements are specific to this test, not a promise for every camera, scene, model, or GPU. See [1.2.5 patch notes](docs/releases/1.2.5.md) and [test instructions](tests/README.md).

## Current status

- ✅ Stable NR processing (CPU-staged) — recommended
- ✅ Temporal mode: NVIDIA Optical Flow motion vectors (better stability on movement; CPU path only, not available with zero-copy GPU mode)
- ⚠️ Zero-copy GPU path: experimental, **off by default**. The NR runtime currently aborts the process when writing into D3D11-shared textures at 4K. Enable only for testing.

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
- Community tools that bundle the runtime, e.g. **[DLSS5-Swapper](https://github.com/rakanki911/DLSS5-Swapper)** (its bundled 310.8 build is the same file most of the community ships)

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

The release is **one generic Windows x64 ZIP** for all runtime-supported RTX GPUs, with no 5090-only compiler target or GPU-generation allowlist. Only the RTX 5090 was hardware-tested for 1.2.5; support on other generations remains dependent on NVIDIA's runtime and driver.

The community-standard **310.8 runtime** (the build everyone ships, including the DLSS5-Swapper project) reports support across the full RTX lineup:

| GPU | Status |
|---|---|
| RTX 50xx | ✅ Verified (development and testing happen on a 5090) |
| RTX 40xx | Reported working with the standard 310.8 runtime |
| RTX 30xx | Reported working with the standard 310.8 runtime |
| RTX 20xx | Reported working with the standard 310.8 runtime |
| Older / non-RTX / AMD / Intel | Not supported |

"Reported" = per the community projects that ship this runtime; we've only verified 50-series ourselves. If the Status line shows `0xBAD00001`, your runtime build does not support your GPU/driver combination — try a different build.

## Building from source

Same toolchain as [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate): Visual Studio 2022 (or Build Tools), CMake ≥ 3.28, Windows SDK 10.0.22621+.

```powershell
cmake -S . -B build_x64
cmake --build build_x64 --config RelWithDebInfo
```

Non-Windows platforms build an empty module (the filter only registers on `_WIN32`).

## Releases

Semantic-version tags (for example `1.2.5`) trigger the Windows-only build and regression tests. The release workflow uses **curl** to create a draft with versioned patch notes and a checksum, attaching only the Windows x64 ZIP. The ZIP includes the README, license, and patch notes. GitHub also displays its automatic source-code links. See [release instructions](docs/RELEASING.md).

## License

GPL-2.0 (see `LICENSE`). Integration follows the NVIDIA NGX / DLSS SDK interface — see `THIRD_PARTY_NOTICES.md`.
