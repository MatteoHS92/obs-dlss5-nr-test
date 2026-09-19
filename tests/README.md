# Performance regression tests

Configure with `-DENABLE_PERFORMANCE_TESTS=ON`, build, then run:

```powershell
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

The frame-policy test needs no GPU. It checks the real limiter against matching 15/24/30/60 FPS clocks with jitter, source-rate downsampling, rate changes, pauses, and processing-size/aspect-ratio rules.

## Manual GPU smoke test

Also configure with `-DENABLE_GPU_SMOKE_TEST=ON`. The harness uses an animated synthetic 4K source, not the user's camera or saved scene collection. It requires OBS installed at `C:\Program Files\obs-studio` and a user-supplied NR runtime in the normal OBS plugin config directory.

```powershell
$env:PATH = 'C:\Program Files\obs-studio\bin\64bit;' + $env:PATH
& .\build_x64\tests\RelWithDebInfo\obs-performance-smoke.exe `
  "$PWD/build_x64/RelWithDebInfo/obs-dlss5-nr.dll" `
  "$PWD/data" `
  "$env:APPDATA/obs-studio/plugin_config" 1
```

Scenarios: `0` full-resolution Low latency + Temporal (baseline), `1` 1080p Smooth, `2` 1080p Smooth + Temporal, `3` 1080p Low latency, `4` all dropdown resolutions and mode transitions, `5` 720p Smooth + Temporal.

The normal runs warm up for five seconds and measure ten seconds. Scenario 4 also switches through all five options for six seconds each and verifies the status dimensions and unchanged output dimensions. The harness checks that output is nonblack and changing, that the five-choice dropdown exists, and (except the legacy baseline) that the plugin reports enhanced output rather than passing raw frames through. Scenarios 1 and 5 additionally require at least 27 changing frames/sec; this performance threshold targets the development RTX 5090 and is not a minimum GPU compatibility requirement.
