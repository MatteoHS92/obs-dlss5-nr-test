# obs-dlss5-nr

**Unofficial DLSS 5 Neural Rendering filter for OBS Studio** — runs NVIDIA DLSS 5 NR (NGX feature 18) on any video source (Game Capture, Media Source, Webcam, …) with a **Default / Natural / Cinematic** style selector.

> ⚠️ **Windows x64 + NVIDIA RTX only.** Linux/macOS builds compile as no-ops.
> This project is not affiliated with, endorsed by, or supported by NVIDIA or the OBS Project. It targets an undocumented, pre-release interface; behavior may change with NVIDIA driver or `nvngx_dlssnr.dll` versions.

## Status — Phase 1

- [x] Plugin skeleton from obs-plugintemplate, renamed `obs-dlss5-nr`
- [x] Pass-through video filter (copies input → output unchanged)
- [x] Full properties UI: Style (Default/Natural/Cinematic), Render Preset, Intensity, Local Tone, Local Structure, Skin, Auto Mask, UI Correction, GPU Index, Channel Order, Reset History
- [ ] **Phase 2** — D3D12 + NGX bridge (`nvngx_dlssnr`, feature 18), CPU-staged frame processing
- [ ] **Phase 3** — release packaging + runtime DLL install instructions
- [ ] **Phase 4** — zero-copy D3D11↔D3D12 interop, optical-flow motion vectors

## Requirements (end users)

- Windows 10/11 x64, NVIDIA RTX GPU, recent driver
- A **legally obtained** `nvngx_dlssnr.dll` placed in the plugin's `data/runtime/` folder (Phase 3). **This project never redistributes NVIDIA binaries.**

## Building

Same toolchain as [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate): Visual Studio 2022, CMake ≥ 3.28, Ninja.

```powershell
cmake --preset windows-x64
cmake --build build-x64 --config Release
```

Non-Windows platforms build an empty module (the filter only registers on `_WIN32`).

## License

GPL-2.0 (see `LICENSE`). Phase 2 will vendor MIT-licensed bridge code from [lisitskyaa/ComfyUI-DLSS5-NR](https://github.com/lisitskyaa/ComfyUI-DLSS5-NR) — see `THIRD_PARTY_NOTICES.md`.
