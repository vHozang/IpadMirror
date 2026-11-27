# Low-Latency Optimization & Diagnostics Throttling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Minimize AirPlay screen mirroring latency by enabling `TCP_NODELAY` and tightening the video pipeline queue to 1 buffer, while slashing CPU overhead by throttling diagnostics updates to the Qt event loop at 4 Hz (250ms).

**Architecture:**
- Socket layer: Configure `TCP_NODELAY` on both the real-time mirror stream socket (`stream_fd`) in `raop_rtp_mirror.c` and RTSP control sockets in `httpd.c` to disable Nagle's algorithm.
- Video pipeline: Adjust downstream GStreamer video queue in `video_renderer.c` from `max-size-buffers=3` to `max-size-buffers=1 leaky=downstream` to prevent frame buffering lag.
- Diagnostics dispatcher: Rate-limit diagnostic callbacks in `UxPlayReceiver.cpp` and `AirPlayController.cpp` to at most once per 250ms (4 Hz) instead of dispatching on every video frame (60 Hz).

**Tech Stack:** C11, C++20, Winsock2 (`setsockopt`, `IPPROTO_TCP`, `TCP_NODELAY`), GStreamer D3D11, Qt 6.8 Widgets.

**Spec Reference:** User-requested Priorities #1 (Latency optimization) and #2 (Diagnostics throttling & clean CPU overhead).

## Global Constraints

- Portable tests (`padmirror_support`) must remain clean without Qt or GStreamer dependencies.
- Zero frame corruption or decoder breakdown: leaky queue must remain downstream of decoder/converter, never before the parser/decoder.
- State changes (`ReceiverState::Streaming`, `ReceiverState::Waiting`, etc.) must never be throttled or delayed.

## Review Focus

1. **`TCP_NODELAY` portability:** Must compile and execute cleanly on Windows Winsock (`IPPROTO_TCP`, `TCP_NODELAY`).
2. **Video queue overrun behavior:** With `max-size-buffers=1 leaky=downstream`, the overrun signal in `video_renderer.c` must still increment `queue_dropped` stats correctly without pipeline stalls.
3. **Smooth video playback:** Dropping queue buffer depth to 1 must produce immediate frame presentation without stutter on 60 FPS streams.
4. **Immediate state propagation:** Connection and disconnection events must display immediately in the UI even when diagnostics are throttled.
5. **No memory leak in throttled diagnostics:** Lambda captures in `QMetaObject::invokeMethod` must only occur when a diagnostics frame is actually emitted.

---

### Task 1: Low-Latency Socket & Video Pipeline Configuration

**Files:**
- Modify: `third_party/uxplay-native/lib/raop_rtp_mirror.c:260-280`
- Modify: `third_party/uxplay-native/lib/httpd.c:295-310`
- Modify: `third_party/uxplay-native/renderers/video_renderer.c:645-655`

**Interfaces:**
- Socket level: `setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ...)` applied to active TCP connections.
- Pipeline level: `queue name=video_live_queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream` in GStreamer launch string.

- [ ] **Step 1: Enable `TCP_NODELAY` on mirror data stream socket in `raop_rtp_mirror.c`**

In `raop_rtp_mirror.c`, immediately after `accept(...)` succeeds for `stream_fd`:
```c
int nodelay = 1;
if (setsockopt(stream_fd, IPPROTO_TCP, TCP_NODELAY, CAST &nodelay, sizeof(nodelay)) < 0) {
    int sock_err = SOCKET_GET_ERROR();
    logger_log(raop_rtp_mirror->logger, LOGGER_WARNING,
               "raop_rtp_mirror could not set TCP_NODELAY: %d %s", sock_err, SOCKET_ERROR_STRING(sock_err));
}
```

- [ ] **Step 2: Enable `TCP_NODELAY` on HTTP/RTSP client connections in `httpd.c`**

In `httpd.c`, inside `httpd_accept_connection(...)`, immediately after `accept(...)` returns `fd`:
```c
int nodelay = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &nodelay, sizeof(nodelay));
```

- [ ] **Step 3: Update `video_live_queue` buffer limit in `video_renderer.c`**

In `video_renderer.c` around line 649:
Change `max-size-buffers=3` to `max-size-buffers=1`:
```c
g_string_append(launch, "queue name=video_live_queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! ");
```

- [ ] **Step 4: Build target `padmirror_uxplay_native` to verify compilation**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -SkipInstaller`
Expected: Compilation succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add third_party/uxplay-native/lib/raop_rtp_mirror.c third_party/uxplay-native/lib/httpd.c third_party/uxplay-native/renderers/video_renderer.c
git commit -m "perf(media): enable TCP_NODELAY and tighten video queue to 1 buffer"
```

---

### Task 2: Diagnostics Throttling to Reduce Qt Event Loop Overhead

**Files:**
- Modify: `src/airplay/UxPlayReceiver.cpp:50-88, 270-290, 320-340`
- Modify: `src/app/AirPlayController.cpp:25-35`

**Interfaces:**
- Produces: Throttled dispatch of `diagnosticsCallback` so UI event loop is not saturated at 60 Hz.
- Guarantees: State changes via `publish()` are delivered immediately without throttling.

- [ ] **Step 1: Add timestamp rate-limiting in `ReceiverContext` (`UxPlayReceiver.cpp`)**

In `ReceiverContext`:
Add `std::atomic<std::int64_t> lastDiagnosticsDispatchMs{0};`
In `updateDiagnostics(Update update)`:
Always apply `update(diagnostics)` under lock to keep current snapshot accurate.
Only trigger `diagnosticsCallback(snapshot)` if `now - lastDiagnosticsDispatchMs >= 250` (or on first run):
```cpp
const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
const auto last = lastDiagnosticsDispatchMs.load(std::memory_order_relaxed);
if (now - last >= 250) {
    lastDiagnosticsDispatchMs.store(now, std::memory_order_relaxed);
    if (diagnosticsCallback) {
        diagnosticsCallback(snapshot);
    }
}
```

- [ ] **Step 2: Ensure `publish()` always immediately updates and delivers diagnostics**

In `publish(ReceiverState state, std::string detail)`:
Reset `lastDiagnosticsDispatchMs.store(now, std::memory_order_relaxed);` so that any state change (Starting, Streaming, Error, Stopping) immediately delivers the latest diagnostics snapshot to the UI without waiting.

- [ ] **Step 3: Build and test full application**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -SkipInstaller`
Expected: Build succeeds, portable tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/airplay/UxPlayReceiver.cpp src/app/AirPlayController.cpp
git commit -m "perf(diagnostics): throttle diagnostics dispatch to 4 Hz to cut Qt event loop overhead"
```

---

### Task 3: Full Package Build, Runtime Check & Deployment to `D:\App\PadMirror`

**Files:**
- Output: `dist/PadMirrorSetup.exe`
- Target: `D:\App\PadMirror`

- [ ] **Step 1: Run all unit tests**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1`
Expected:
- CTest passes 100%.
- Installer `PadMirrorSetup.exe` is built and verified.
- Stage integrity manifest updated.

- [ ] **Step 2: Update deployed app in `D:\App\PadMirror`**

Run silent installer:
`.\dist\PadMirrorSetup.exe /VERYSILENT /SUPPRESSMSGBOXES /DIR="D:\App\PadMirror" /NORESTART`
Expected: Exit code 0, `D:\App\PadMirror\PadMirror.exe` updated.

- [ ] **Step 3: Run runtime validation**

Run: `& "D:\App\PadMirror\PadMirrorRuntimeCheck.exe"`
Expected: Output `OK`.

- [ ] **Step 4: Commit and push to branch `Stable`**

```bash
git push origin Stable
```
