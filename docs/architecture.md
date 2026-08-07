# Kiến trúc triển khai

PadMirror giữ đúng mục tiêu V1: USB trước, latency thấp, 60 FPS ổn định, audio đến sớm và không tích lũy trễ.

## USB Gaming

```text
iPad
  -> libusb / QuickTime hidden USB configuration
  -> QuickTimeProtocol
  -> PacketParser
     -> H.264 Annex B -> GStreamer -> hardware decoder -> GPU sink
     -> PCM S16LE    -> 40 ms hard-cap ring buffer -> audio sink
```

- `UsbTransport` bật control request `0x40/0x52`, tìm interface subclass `0x2A`, claim bulk endpoints và tự reconnect.
- `QuickTimeProtocol` xử lý `PING`, `SYNC`, `ASYN`, `FEED`, `EAT!`, `NEED`, `RELS` và shutdown có kiểm soát.
- `PacketParser` đọc CoreMedia sample buffer, CMTime, H.264 AVCC/SPS/PPS và PCM.
- Video dùng hai queue 1 frame, `leaky=downstream`, render frame mới nhất.
- Audio mặc định prime ở 10 ms; khi tổng backlog vượt 40 ms, dữ liệu cũ bị bỏ để quay về live edge.
- Audio không bị giữ lại chỉ để khớp video trong Gaming Mode.

## Backend nền tảng

| Nền tảng | Video | Audio |
| --- | --- | --- |
| Windows 10 x64 | `d3d11h264dec` -> `d3d11videosink` | `wasapi2sink`, fallback `wasapisink` |
| macOS | `vtdec_hw`, fallback `vtdec` | `osxaudiosink` |

`libimobiledevice` là tùy chọn để đọc tên, UDID và trạng thái trust. Capture data vẫn đi trực tiếp qua `libusb`; GUI không gọi USB/GStreamer trực tiếp.

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
- USB read và protocol dispatch chạy trên worker thread.
- Decode/render/audio scheduling do GStreamer quản lý.
- Device scan chạy nền; cập nhật UI được đưa về Qt event loop.
- Metrics dùng counter nhỏ, thread-safe và refresh UI mỗi 250 ms.

## Phạm vi V1

Không triển khai recording, screenshot, OBS output, filter, HDR, upscale, remote control, multi-device hoặc thiết kế 120 FPS. Wi-Fi là fallback, không phải đường competitive gaming chính.
