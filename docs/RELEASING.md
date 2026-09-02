# Releasing

1. Bump `"version"` in `buildspec.json` (semantic version, no leading `v`).
2. Commit, then tag the release:

   ```powershell
   git tag 0.3.0
   git push origin master --tags
   ```

3. GitHub Actions (`push` → `pr-pull`) builds Windows x64 packages and opens a
   **draft release** with the package attached. Review it, add release notes,
   publish.

## Release checklist

- [ ] `buildspec.json` version matches the tag
- [ ] CI is green, including the *No proprietary binaries* check
- [ ] Package contents: `obs-dlss5-nr.dll`, `locale/`, this README — **never** any `nvngx*` / `_nvngx*` NVIDIA binary
- [ ] Release notes mention that users must supply `nvngx_dlssnr.dll` themselves and where to put it
- [ ] Verify the plugin loads in a clean OBS 31.1 portable install before publishing
