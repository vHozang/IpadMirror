# Kiến trúc PadMirror

PadMirror V1 là receiver **AirPlay-only cho Windows 10/11 x64**. Ứng dụng chỉ có một backend và một media path; không giữ abstraction đa-transport hoặc media pipeline cạnh tranh.

## Runtime graph

```text
PadMirror.exe (Qt Widgets)
  MainWindow / SettingsDialog / DiagnosticsDialog
                       |
                AirPlayController
                       |
                UxPlayReceiver
                       |
            padmirror_uxplay_native
        RAOP/RTSP + pairing/crypto + mDNS
             |                       |
       video_renderer          audio_renderer
             |                       |
 H.264/H.265 → GStreamer       AAC-ELD/ALAC → PCM
 → D3D11 decode/render         → GStreamer → WASAPI2
```

Các CMake target chính:

- `padmirror_support`: normalize `ReceiverConfig` và tạo receiver identity ổn định; không phụ thuộc Qt/GStreamer.
- `padmirror_libplist`: libplist C được vendor cho Apple property lists.
- `padmirror_uxplay_native`: UxPlay-derived RAOP/RTSP, crypto, mDNS và GStreamer renderers.
- `padmirror_airplay`: biên C++ RAII `UxPlayReceiver` bao quanh native API.
- `padmirror_app_support`: Qt Widgets UI, controller, settings, logging và runtime validation.
- `padmirror_app`: `PadMirror.exe`.
- `padmirror_runtime_check`: validator độc lập dùng cho release stage.

Không có USB/QuickTime backend, `MirrorCoordinator`, `IMirrorBackend`, `IMediaIngress`, loopback HTTP API, QML hoặc external receiver process.

## Lifecycle receiver

`AirPlayController` sở hữu đúng một `UxPlayReceiver` và là adapter giữa Qt UI thread với native callback threads.

Start:

1. Normalize receiver name, max FPS và audio settings.
2. Tạo stable locally-administered unicast MAC từ receiver identity.
3. Khởi tạo GStreamer video/audio renderer với HWND hiện tại.
4. Tạo RAOP receiver và đầy đủ callback mặc định.
5. Bind RTSP/RAOP listener.
6. Chỉ sau khi listener hoạt động mới publish `_raop._tcp` và `_airplay._tcp` qua mDNS.
7. Chuyển state sang `Waiting`/Ready.

Stop theo thứ tự ngược: dừng quảng bá, hủy RAOP/DNS-SD, rồi hủy renderer/logger. Start/stop/restart được serialize; thay receiver name hoặc giới hạn FPS gây restart có kiểm soát, còn volume/mute được áp dụng trực tiếp.

## State model

Native receiver phát các state:

- `Starting`: chuẩn bị runtime/listener.
- `Waiting`: receiver đã listen và sẵn sàng được chọn trên iPad; UI hiển thị **Ready**.
- `Connecting`: một AirPlay client đang thiết lập phiên.
- `Streaming`: media đang được nhận/render.
- `Stopping`: receiver đã hoặc đang dừng; UI hiển thị **Stopped**.
- `Error`: lỗi runtime, bind, mDNS hoặc renderer.

Callback native không thao tác widget trực tiếp. `AirPlayController` dùng queued invocation để marshal state và diagnostics về Qt event loop. Các giá trị volume/mute được chia sẻ với callback threads bằng atomic.

## Video path

AirPlay cung cấp H.264 hoặc H.265 access units. Pipeline native dùng:

```text
appsrc
  → h264parse/h265parse
      config-interval=-1
      disable-passthrough=true
  → d3d11h264dec/d3d11h265dec
  → d3d11convert
  → queue max-size-buffers=3 leaky=downstream
  → videoscale
  → d3d11videosink
```

Nguyên tắc quan trọng:

- Không đặt leaky queue trước parser/decoder: bỏ access unit mã hóa có thể phá prediction chain.
- Queue tối đa ba buffer chỉ nằm sau decode/conversion, vì vậy chỉ frame đã decode cũ bị bỏ để về live edge.
- Renderer nhận HWND của Qt `WA_NativeWindow` qua `gst_video_overlay_set_window_handle`.
- Sink giữ aspect ratio khi resize/fullscreen.
- `d3d11convert` xử lý memory/caps và color-range trước sink.
- Annex-B NAL inspection đánh dấu H.264 IDR (type 5) và H.265 BLA/IDR/CRA (types 16–21); access unit khác mang `DELTA_UNIT`.
- Buffer đầu sau start/flush mang `DISCONT`; flush reset pipeline timing/discontinuity.
- Property decoder low-latency chỉ được set khi runtime factory thực sự expose property đó. Điều này tránh parse/start failure giữa các bản GStreamer/driver.

PadMirror yêu cầu D3D11 H.264 và H.265 decoder trong runtime check; không quảng bá software fallback như một đường được hỗ trợ.

## Audio path

AirPlay Screen Mirroring thường gửi AAC-ELD (AOT-39). `avdec_aac` không phải decoder phù hợp cho format này, nên renderer load động `libfdk-aac-2.dll` và decode AAC-ELD in-process. ALAC sử dụng GStreamer `avdec_alac`.

PCM đi qua:

```text
appsrc
  → queue max-size-time=200 ms leaky=no
  → audioconvert
  → audioresample quality=10
  → volume
  → level
  → wasapi2sink
      low-latency=false
      buffer-time=90 ms
      latency-time=20 ms
      sync=true
```

Timestamp NTP từ client được re-anchor sang clock cục bộ. Burst governor dựa trên `g_get_monotonic_time()` và bỏ input khi lượng audio đã push vượt wall-clock hơn 150 ms, tránh tích lũy burst mà không phụ thuộc WASAPI sink clock. Flush reset GStreamer, decoder AAC-ELD, wall-clock accounting và discontinuity state.

## UI và settings

UI dùng Qt Widgets:

- `MainWindow` tạo native video host, status card và toolbar.
- F11/double-click chuyển fullscreen; Escape thoát fullscreen.
- `SettingsDialog` lưu receiver name, 30/60 FPS, volume/mute, keep-awake, start-on-launch và fullscreen-on-connect.
- `DiagnosticsDialog` hiển thị listener, connection/client, codec/resolution/sample rate, backend/runtime và log path.
- `QSettings` lưu cấu hình theo application identity `PadMirror`.
- Khi streaming, controller có thể gọi `SetThreadExecutionState` để giữ display thức theo setting.

## Runtime validation và logs

`RuntimeDependencies` cấu hình plugin path/registry riêng cho package rồi kiểm tra:

- `appsrc`, `queue`, H.264/H.265 parsers.
- D3D11 H.264/H.265 decoders, converter và sink.
- audio convert/resample, volume, level, ALAC decoder và WASAPI2 sink.
- `libfdk-aac-2.dll`.

`PadMirrorRuntimeCheck.exe` và `PadMirror.exe --check-runtime` dùng cùng validator. Native stderr được redirect vào `gstreamer.log`; Qt/application messages đi vào `padmirror.log`. Mỗi log rotate ở 4 MiB và giữ một bản `.1`.

## Network và security boundary

Receiver listen trên LAN để AirPlay hoạt động; không có HTTP automation API. mDNS được bundle cùng native backend, không cần Bonjour SDK/service bên ngoài.

Firewall helper:

- Chỉ tạo inbound rules gắn với đường dẫn tuyệt đối của `PadMirror.exe`.
- Chỉ áp dụng Private và Domain profiles.
- Tạo rule TCP và UDP cho in-process receiver/mDNS.
- Không chạy trong build, runtime hoặc portable package nếu người dùng không gọi rõ ràng.
- Installer task bỏ chọn mặc định và cần UAC khi được chọn.

## Licensing và phạm vi

`PadMirror.exe` liên kết mã UxPlay-derived GPLv3, vì vậy sản phẩm được phân phối theo GPL-3.0-or-later và phải đi kèm/cho truy cập Corresponding Source phù hợp cùng notices. Xem `LICENSES/THIRD_PARTY.md`.

V1 không có USB capture, OBS, recording, screenshot, filter/HDR, remote control, second display, multi-device hay 120 FPS.
