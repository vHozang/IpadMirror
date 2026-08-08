# Kiến trúc triển khai

PadMirror giữ đúng mục tiêu V1: USB trước, latency thấp, 60 FPS ổn định, audio đến sớm và không tích lũy trễ.

## USB Gaming

```text
iPad
  -> Apple Mobile Device Service / trust
  -> RemoteXPC user-space tunnel
  -> Apple DisplayService
     -> HEVC RTP    -> PadMirrorUsbBridge.exe -> GStreamer -> GPU sink
     -> AAC-ELD RTP -> PadMirrorUsbBridge.exe -> GStreamer -> WASAPI
```

- Windows chạy `PadMirrorUsbBridge.exe` ở tiến trình riêng qua `QProcess`; media frame đi về app bằng stdout nhị phân có giới hạn kích thước.
- Bridge dùng Apple pairing, Developer Mode, Developer Disk Image và `UserspaceRsdTunnel`; không cài hoặc gọi UsbDk/libusb0.
- HEVC dùng D3D11/NVIDIA khi có, fallback `avdec_h265`; AAC-ELD được giải mã bằng `avdec_aac`.
- Video dùng hai queue 1 frame, `leaky=downstream`, render frame mới nhất.
- Audio không bị giữ lại chỉ để khớp video trong Gaming Mode.

Backend QuickTime/libusb cũ (`QuickTimeProtocol` + `PacketParser`, H.264/PCM) chỉ còn cho nền tảng non-Windows và kiểm thử protocol; binary Windows không link hay mở raw USB.

## Backend nền tảng

| Nền tảng | Video | Audio |
| --- | --- | --- |
| Windows USB | `d3d11h265dec`, fallback NVIDIA/libav -> `d3d11videosink` | `avdec_aac` -> `wasapi2sink`, fallback `wasapisink` |
| Windows Wi-Fi | `d3d11h264dec` -> `d3d11videosink` | L16/PCM -> WASAPI |
| macOS | `vtdec_hw`, fallback `vtdec` | `osxaudiosink` |

GUI không gọi Apple USB API hoặc GStreamer trực tiếp. `CaptureSession` quản lý bridge; `MediaSession` quản lý pipeline decode/render/audio.

## AirPlay cùng Wi-Fi

```text
iPad --AirPlay--> UxPlay (tiến trình riêng)
                    -> H.264 RTP 127.0.0.1:50100
                    -> L16 RTP  127.0.0.1:50102
                    -> PadMirror GStreamer pipeline
```

UxPlay không được nhúng hoặc link vào PadMirror. Ranh giới tiến trình giữ phần GPLv3 tách khỏi mã ứng dụng; khi phân phối vẫn phải kèm thông báo và tuân thủ license của UxPlay.

## Thread model

- Qt/QML chỉ chạy trên UI thread.
- Apple USB/RemoteXPC chạy trong tiến trình bridge riêng; Qt đọc stdout/stderr bất đồng bộ.
- Decode/render/audio scheduling do GStreamer quản lý.
- Device scan chạy nền; cập nhật UI được đưa về Qt event loop.
- Metrics dùng counter nhỏ, thread-safe và refresh UI mỗi 250 ms.

## Phạm vi V1

Không triển khai recording, screenshot, OBS output, filter, HDR, upscale, remote control, multi-device hoặc thiết kế 120 FPS. Wi-Fi là fallback, không phải đường competitive gaming chính.
