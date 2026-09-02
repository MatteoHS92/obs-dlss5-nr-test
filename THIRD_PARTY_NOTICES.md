# Third-party notices

## NVIDIA binaries — NOT included

This project does not redistribute any NVIDIA software:

- `_nvngx.dll` (NGX core)
- `nvngx_dlssnr.dll` (DLSS 5 Neural Rendering runtime)
- `nvngx_dlss.dll` or any other `nvngx_*` runtime
- NGX SDK headers

Users must supply a legally obtained `nvngx_dlssnr.dll` (Phase 3 install instructions). NVIDIA, DLSS, and NGX are trademarks of NVIDIA Corporation. This project is unofficial and not affiliated with NVIDIA.

## ComfyUI-DLSS5-NR (planned, Phase 2)

The D3D12/NGX bridge planned for Phase 2 is derived from
[lisitskyaa/ComfyUI-DLSS5-NR](https://github.com/lisitskyaa/ComfyUI-DLSS5-NR)
(`dlss5nr_bridge.cpp`, `caller_shim.cpp`), MIT License, Copyright (c) 2026
ComfyUI-DLSS5-NR contributors. Its undocumented-interface findings (NGX feature
18 parameter names, caller-validation shim, DriverStore `_nvngx.dll` discovery)
are reused with attribution under the MIT license.

## obs-plugintemplate

Project scaffolding, CMake build system, and GitHub Actions workflows from
[obsproject/obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
