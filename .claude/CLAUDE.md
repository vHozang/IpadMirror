# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Product

PadMirror V1 is a **Windows 10/11 x64 AirPlay Screen Mirroring receiver**. One runtime path, no abstractions:

```
iPad → AirPlay/RAOP (LAN) → UxPlay native compiled into PadMirror.exe
     → GStreamer D3D11 video + FDK AAC/ALAC audio → Qt Widgets UI
```

Do **not** reintroduce: USB/QuickTime capture, Apple driver probing/rebinding,
libusb/UsbDk/Zadig, Python bridge, multi-transport abstractions, duplicate
media pipelines, QML/Qt Quick, loopback API, OBS integration, external
`uxplay.exe`, or RTP relay.

V1 excludes: recording, screenshots, filtering/HDR, remote control,
second-display, multi-device, 120 FPS target.

## Build commands

### Portable tests (no Qt/GStreamer needed, works on any OS)

```bash
cmake -S . -B build/core -DPADMIRROR_BUILD_APP=OFF -DPADMIRROR_BUILD_TESTS=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Tests cover receiver config normalization and deterministic identity/MAC generation. They do not prove iPad compatibility.

### Run a single test

```bash
ctest --test-dir build/core --output-on-failure -R receiver_config_test
```

### Full Windows release build (from WSL)

```
psrun ./scripts/build-windows.ps1
```

Or from PowerShell directly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

The script provisions MSVC 2022 x64, Qt 6.8.3, CMake/Ninja, GStreamer MSVC
1.28.5 and Inno Setup. It builds, runs CTest, stages dependencies, validates
the stage, scans forbidden payloads, and creates ZIP/installer/checksums.

Use `-SkipInstaller` for faster iteration. Final verification must run without it.

### Manual build (dependencies already available)

```powershell
$env:PKG_CONFIG_PATH = "$env:GSTREAMER_ROOT_X86_64\lib\pkgconfig;$env:GSTREAMER_ROOT_X86_64\share\pkgconfig"
$env:PATH = "$env:GSTREAMER_ROOT_X86_64\bin;$env:PATH"

cmake -S . -B build\windows-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:QTDIR" `
  -DPADMIRROR_OPENSSL_ROOT="$env:GSTREAMER_ROOT_X86_64" `
  -DPADMIRROR_BUILD_APP=ON -DPADMIRROR_BUILD_TESTS=ON

cmake --build build\windows-x64 --parallel
ctest --test-dir build\windows-x64 --output-on-failure
```

### Runtime validation (on staged build)

```powershell
.\build\windows-x64\stage\PadMirrorRuntimeCheck.exe
.\build\windows-x64\stage\PadMirror.exe --check-runtime
```

## Architecture

### CMake targets

```
padmirror_support       → ReceiverConfig + ReceiverIdentity (portable, no Qt/GStreamer)
padmirror_libplist      → vendored libplist C core (Apple property lists)
padmirror_uxplay_native → UxPlay-derived RAOP/RTSP, crypto, mDNS, GStreamer renderers
padmirror_airplay       → C++ RAII UxPlayReceiver wrapping the native C API
padmirror_app_support   → Qt Widgets UI, AirPlayController, settings, logging, runtime checks
padmirror_app           → PadMirror.exe
padmirror_runtime_check → PadMirrorRuntimeCheck.exe (standalone validator)
```

`padmirror_support` is the only target that builds without Qt/GStreamer (`-DPADMIRROR_BUILD_APP=OFF`). All tests currently link against it.

### Ownership and threading

`AirPlayController` owns exactly one `UxPlayReceiver`. Native callbacks fire on
background threads; `AirPlayController` marshals state/diagnostics to the Qt
event loop via `QMetaObject::invokeMethod` with `Qt::QueuedConnection`. Volume
and mute are shared with callback threads via atomics.

`ApplicationSettings` emits `configChanged`; the controller compares old/new
config to decide whether a full restart is needed (name or FPS change) or a
live update suffices (volume/mute).

### Startup sequence

`main.cpp` → `RuntimeDependencies::configureEnvironment()` → validate
components → `ApplicationSettings` → `AirPlayController` → `MainWindow::show()`
→ deferred `startReceiver()`.

The receiver starts by: normalizing config → generating stable MAC →
initializing GStreamer renderers with HWND → creating RAOP receiver → binding
listener → only then publishing `_raop._tcp` and `_airplay._tcp` via mDNS.
Shutdown reverses this order.

### Namespaces

- `padmirror::support` — portable config/identity (no framework deps)
- `padmirror::airplay` — `UxPlayReceiver`, `ReceiverState`, `ReceiverDiagnostics`
- `padmirror::app` — Qt layer: `AirPlayController`, `ApplicationSettings`, `MainWindow`, `SettingsDialog`, `DiagnosticsDialog`, `Logging`, `RuntimeDependencies`

## Media invariants

### Video pipeline

```
appsrc → h264parse/h265parse → d3d11h264dec/d3d11h265dec
       → d3d11convert → queue(max 3, downstream-leaky) → videoscale → d3d11videosink
```

- Never place a leaky queue before the parser/decoder — dropping encoded AUs breaks the prediction chain.
- Preserve keyframe marking (`DELTA_UNIT`), first-buffer `DISCONT`, and real flush/reset.
- Set decoder low-latency properties only if the installed element actually exposes them.
- Render into the native Qt widget HWND; preserve aspect ratio on resize/fullscreen.

### Audio pipeline

```
AAC-ELD AOT-39 → dynamically loaded libfdk-aac-2.dll → PCM
ALAC → avdec_alac → PCM
PCM → non-leaky 200 ms queue → audioconvert → audioresample → volume → level → wasapi2sink
```

- Remote NTP timestamps must be re-anchored to local monotonic time.
- Burst governor caps audio lead at 150 ms using `g_get_monotonic_time()`.
- Flush must reset GStreamer, FDK decoder state, wall-clock accounting, and discontinuity state.

## Packaging rules

The release scanner rejects: external `uxplay.exe`, QML/QtQml, USB/QuickTime/driver payloads, libusb/UsbDk, Python bridges, OBS artifacts. Portable packages never modify firewall. The installer firewall task is unchecked by default, requires UAC, and creates program-scoped inbound TCP/UDP rules for Private/Domain only.

## Licensing

GPL-3.0-or-later (links UxPlay-derived GPLv3 code). Preserve `LICENSE`,
`LICENSES/THIRD_PARTY.md`, all vendored license texts, FDK AAC custom license,
and staged Qt/GStreamer license inventories. Binary releases must provide
Corresponding Source and satisfy FDK AAC source/patent-notice requirements.

## Verification checklist

Before declaring rewrite or release work complete:

1. Run portable tests.
2. Run the full Windows build and CTest.
3. Run both staged runtime checks.
4. Confirm ZIP, installer, and checksum outputs.
5. `git diff --check` the final diff.
6. Launch the staged app: start → ready → stop → restart → fullscreen → logging → clean shutdown (no automatic firewall mutation).
7. iPad end-to-end (H.264/H.265, AAC-ELD, reconnect, orientation, seek, long-session A/V sync, sleep/wake) is hardware validation — if hardware is unavailable, report that blocker explicitly.

## Long-running work

Continue autonomously through inspect → implement → build → test → debug →
retest → verify. Context compaction is not completion. Preserve the objective,
decisions, modified files, verification evidence, failures, blockers, and exact
next action, then resume without asking "continue."
