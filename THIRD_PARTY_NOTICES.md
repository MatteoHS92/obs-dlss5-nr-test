# Third-party notices

## NVIDIA software — NOT included

This project does not redistribute any NVIDIA software. Users must supply their
own legally obtained DLSS Neural Rendering runtime (`nvngx_dlssnr.dll`); the
NGX core (`_nvngx.dll`) is loaded from the local NVIDIA driver installation at
runtime.

The plugin integrates with the NVIDIA NGX / DLSS SDK D3D12 interface
(`NVSDK_NGX_D3D12_*`) following the conventions of NVIDIA's public NGX
programming documentation bundled with the
[DLSS SDK](https://github.com/NVIDIA/DLSS). NVIDIA, DLSS, and NGX are
trademarks of NVIDIA Corporation. This project is unofficial and is not
affiliated with, endorsed by, or supported by NVIDIA.

## obs-plugintemplate

Project scaffolding, CMake build system, and GitHub Actions workflows are based
on [obsproject/obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
