# Kiến trúc triển khai

PadMirror V1 chỉ hỗ trợ Windows 10/11 x64, chiều iPad -> PC. Mục tiêu: latency thấp,
audio đến sớm và không tích lũy trễ. Hai transport loại trừ lẫn nhau (USB **hoặc**
AirPlay), dùng chung một media stack; không second-display, remote-control, OBS,
capture window, macOS hay Linux.

## Module graph

```text
padmirror_core
|- padmirror_media
|- padmirror_usb
|- padmirror_airplay
`- padmirror_api

padmirror_app -> tất cả module trên
```

Dependency một chiều: `padmirror_usb` và `padmirror_airplay` không include hoặc gọi
lẫn nhau, và `padmirror_media` không biết transport cụ thể. Chỉ `padmirror_app`
(và qua `MirrorCoordinator`) mới ghép các module lại.

- `padmirror_core`: domain model (`MirrorTransport`, `MirrorState`,
  `DiscontinuityReason`), packet contract dùng chung, interface `IMediaIngress` /
  `IMirrorBackend`, `MediaEpochController`, `MirrorCoordinator` và các lớp RAII cho
  Windows handle, socket, GStreamer object, Bonjour registration, thread ownership.
- `padmirror_media`: một `MediaEngine` duy nhất dùng chung cho cả hai transport.
- `padmirror_usb`, `padmirror_airplay`: hai backend `IMirrorBackend` độc lập, cả hai
  đang ở trạng thái gated (xem bên dưới).
- `padmirror_api`: local API bind `127.0.0.1`.

## Packet contract dùng chung

Cả USB và AirPlay đẩy media vào `MediaEngine` qua `IMediaIngress`, dùng chung hai
struct đóng gói:

- `EncodedVideoPacket`: codec, payload, `ptsNs`, `epoch`, `keyframe`,
  `discontinuity`, `format`.
- `EncodedAudioPacket`: codec, payload, `ptsNs`, `epoch`, `discontinuity`, `format`.

Backend chỉ tạo packet và gọi `pushVideo` / `pushAudio` / `signalDiscontinuity` /
`endOfStream`; media không cần biết dữ liệu đến từ USB hay AirPlay.

## MirrorCoordinator

`MirrorCoordinator` là owner duy nhất của session:

```text
AppController
    |
MirrorCoordinator
    |- UsbMirrorBackend
    |- AirPlayBackend
    `- MediaEngine
```

Trách nhiệm: bảo đảm USB XOR AirPlay, start/stop/switch transport, flush media trước
mỗi lần chuyển mode, map backend state thành UI/API state, xử lý reconnect và error
policy. Backend không gọi trực tiếp UI; chuyển AirPlay <-> USB là thao tác explicit
của người dùng và không tự ngắt session đang chạy.

## Epoch và discontinuity

`MediaEpochController` quản lý tính liên tục của stream. Khi phát hiện gián đoạn nó
tăng epoch, flush ngay video/audio queue, reset A/V anchor, ghi lại `reason` cùng
timestamp flush và media đầu tiên sau flush, rồi từ chối mọi packet có
`epoch < activeEpoch`.

Ngưỡng và nguyên nhân chính:

- PTS lùi quá 1 ms -> `TimestampReset`.
- PTS nhảy tiến quá 250 ms -> `UserSeekLikeJump`.
- Đổi format video/audio -> `VideoFormatChanged` / `AudioFormatChanged`.
- Chuyển transport -> `TransportSwitch`; reconnect -> `TransportReconnect`.
- A/V lệch quá 50 ms kéo dài -> hard resync (flush và re-anchor ngay, không sửa dần
  trong hàng trăm mili-giây).

## Media stack

`MediaEngine` = GStreamer + D3D11 hardware decode/render + WASAPI.

```text
MediaIngress
|- MediaEpochController
|- TimestampNormalizer
|- VideoPipeline (FrameQueue -> parser -> D3D11 decoder -> D3D11 renderer)
`- AudioPipeline (AudioLiveBuffer -> decoder -> resampler -> WasapiOutput)
```

- Canvas ra `1920x1080`, giữ aspect ratio, letterbox/pillarbox.
- Video queue mục tiêu 0-1 frame, tối đa 3 frame, `leaky=downstream` để luôn render
  frame mới nhất; GPU path không copy frame về CPU khi D3D11 decoder khả dụng.
- Hardware decoder là đường chính; software decoder chỉ là fallback có cảnh báo
  diagnostics.
- Audio 48 kHz, target USB khoảng 8 ms, AirPlay 10-20 ms, hard max 35 ms; queue đầy
  thì bỏ media cũ (drop oldest) để về live edge.
- Không interpolation, không fake 60 FPS; chỉ báo 59-60 FPS khi source thật sự cung
  cấp 60 FPS.

## USB Gaming (gated)

USB dùng kênh AV user-mode do Apple runtime cung cấp, **không** cài/thay/rebind/filter
driver Apple: không rebind `MI_02` sang WinUSB, không libusb, UsbDk, Zadig, không
Python bridge, không Developer Mode/Developer Disk Image/DisplayService.

```text
UsbMirrorBackend
|- AppleRuntimeProbe
|- AppleDeviceRegistry (SetupAPI + libimobiledevice/libusbmuxd)
|- PairingState (Lockdown/Trust)
|- AppleAvChannelFactory
|  |- AppleMuxServiceChannel
|  `- AppleDeviceInterfaceChannel
|- QuickTimeProtocol + PacketParser
`- UsbDiagnostics
```

Backend chỉ qua gate khi mở được AV channel bằng Apple stack hiện hữu. Nếu chỉ có
thể lấy `MI_02` bằng cách thay driver thì backend trả `USB_CAPTURE_UNSUPPORTED` và
dừng sạch, không có fallback nguy hiểm. `QuickTimeProtocol`/`PacketParser` hiện có
được tái sử dụng nhưng chuyển output sang packet contract mới và thêm phát hiện
format/PTS discontinuity.

## AirPlay Wi-Fi (gated, chưa triển khai)

AirPlay sẽ được **port vào chính `PadMirror.exe`** từ mã UxPlay (GPLv3-or-later);
**không** chạy `uxplay.exe` ngoài tiến trình, không RTP loopback relay, không
`QProcess`/`LanReceiver`/`LanPipeline`.

```text
AirPlayBackend
|- BonjourPublisher (_airplay._tcp + _raop._tcp, tên mặc định PadMirror-PC)
|- AirPlaySession / AirPlayControlProtocol / AirPlayPairing+Crypto
|- AirPlayVideoReceiver + AirPlayAudioReceiver
`- AirPlayDiagnostics
```

Protocol AirPlay native **chưa được triển khai**; transport này ở trạng thái gated
cho tới M6/M7 và báo `Unavailable` khi runtime (Bonjour/mDNSResponder, OpenSSL,
libplist) hoặc port UxPlay chưa sẵn sàng. Callback UxPlay khi hoàn thiện sẽ chuyển
thành `EncodedVideoPacket`/`EncodedAudioPacket` và đi qua cùng `MediaEngine`. Vì port
UxPlay GPLv3 vào binary chính, license sản phẩm là GPLv3-or-later kèm source tương ứng.

Khi USB đang streaming, AirPlay connection bị từ chối với trạng thái `BUSY`; cắm USB
trong lúc AirPlay chạy chỉ hiển thị `USB AVAILABLE`, không tự ngắt AirPlay.

## Local API

`padmirror_api` bind `127.0.0.1:17890`, không bind LAN; giới hạn body/header/timeout/
số client/kích thước JSON và từ chối request ngoài loopback. QML vẫn gọi trực tiếp
`AppController` properties/signals; API chỉ là kênh automation/diagnostics bổ sung.

## Thread model

- Qt/QML chỉ chạy trên UI thread.
- USB capability probe, Apple device enumeration và QuickTime protocol chạy trên
  worker của `UsbMirrorBackend`.
- Decode/render/audio scheduling do GStreamer quản lý.
- Device scan chạy nền; cập nhật UI được đưa về Qt event loop.
- Metrics dùng counter nhỏ, thread-safe, refresh UI mỗi 250 ms.

## Phạm vi V1

Không recording, screenshot, filter, HDR, tăng chi tiết bằng AI, remote control,
multi-device hay 120 FPS. Không OBS, không capture window, không second display.
AirPlay là fallback và hiện chưa hoạt động; USB là đường chính nhưng chỉ chạy khi
Apple runtime mở được kênh AV an toàn.
