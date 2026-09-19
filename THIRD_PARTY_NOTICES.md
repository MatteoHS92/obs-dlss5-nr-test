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

## src/bridge — MIT-licensed portions

`src/bridge/caller_shim.cpp`, `src/bridge/nvof_flow.cpp`, and portions of
`src/bridge/nr_bridge.cpp` are
adapted from [lisitskyaa/ComfyUI-DLSS5-NR](https://github.com/lisitskyaa/ComfyUI-DLSS5-NR).
The NVOF D3D11 function-table integration follows the MIT-licensed
[NIGos/dlss5-bridge](https://github.com/NIGos/dlss5-bridge).

MIT License — Copyright (c) 2026 ComfyUI-DLSS5-NR contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
