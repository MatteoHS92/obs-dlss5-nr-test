# Releasing

1. Run the frame-policy regression tests and the manual OBS GPU smoke test described in `tests/README.md`.
2. Update the version in `buildspec.json` and add `docs/releases/<version>.md`. Update the README's feature list and measured results, distinguishing tested hardware from runtime-supported hardware.
3. Commit and push the reviewed changes. Wait for the Windows build, formatting, and proprietary-binary checks to pass.
4. Create and push the matching version tag, for example `git tag 1.2.5` then `git push origin 1.2.5`.
5. The tag workflow builds only Windows x64, runs the frame-policy tests, and uses **curl** to create a draft release. The release body contains the versioned patch notes and ZIP checksum. Its only uploaded asset is `obs-dlss5-nr-<version>-windows-x64.zip`.
6. Verify the ZIP, notes, checks and commit before publishing the draft. GitHub also displays its automatically generated source-code download links; the workflow does not upload source archives, Linux/macOS packages, installers, or separate debug-symbol assets.

## Package requirements

- One generic Windows x64 binary for all GPUs accepted by the user's NVIDIA runtime. Do not add RTX-generation-specific compiler flags or a device-ID allowlist.
- ZIP contains `obs-dlss5-nr/bin/64bit/obs-dlss5-nr.dll`, the project-built `nvngx.dll_obs.dll` caller shim, plugin data/locale, README, license, and patch notes. A PDB may be included inside the ZIP.
- Never package NVIDIA's proprietary `nvngx_dlssnr.dll`, `_nvngx.dll`, or other NVIDIA runtime binaries. The project-built caller shim is intentionally allowed.
- Regression tests run on CI without a GPU. Manual DLSS smoke tests require an installed OBS runtime, a compatible NVIDIA GPU, and a user-supplied DLSS NR runtime. Do not present CI alone as hardware validation.
