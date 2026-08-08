# QuickTime USB protocol notes

Backend này chỉ dùng cho nền tảng non-Windows/legacy và các bài test protocol. Bản Windows dùng Apple Mobile Device Service + RemoteXPC user-space để tránh kernel capture driver và `WDF_VIOLATION`.

File này ghi lại đúng phần protocol đang được triển khai, không cố mô tả các dịch vụ iOS khác.

## Kích hoạt transport

1. Tìm thiết bị Apple vendor ID `0x05ac` có usbmux hoặc QuickTime configuration.
2. Nếu QuickTime configuration chưa xuất hiện, gửi vendor control request:

```text
bmRequestType = 0x40
bRequest      = 0x52
wValue        = 0
wIndex        = 2
```

3. Chờ iPad disconnect/reconnect.
4. Chọn configuration có interface class `0xff`, subclass `0x2a`.
5. Claim bulk IN/OUT endpoints và đọc stream theo frame length little-endian 32-bit.

## Session handshake

```text
iPad -> PING              PadMirror -> PING reply
iPad -> SYNC OG           PadMirror -> RPLY
iPad -> SYNC CWPA         PadMirror -> HPD1 x2, clock RPLY, HPA1
iPad -> SYNC CVRP         PadMirror -> NEED, clock RPLY
iPad -> SYNC CLOK/TIME    PadMirror -> local monotonic clock replies
iPad -> SYNC AFMT/SKEW    PadMirror -> audio format / effective clock-rate replies
```

`SKEW` trả effective audio clock rate gần `48000`, không phải ratio gần `1.0`.

## Media

- `ASYN FEED` chứa video `CMSampleBuffer`.
- `ASYN EAT!` chứa audio `CMSampleBuffer`.
- H.264 AVCC được đổi sang Annex B; SPS/PPS được chèn khi format description thay đổi.
- Audio hiện hỗ trợ Linear PCM S16LE, mặc định 48 kHz stereo.
- Sau mỗi video `FEED`, PadMirror gửi lại `NEED` để giữ stream chạy.

## Shutdown

1. Gửi `HPA0` với device audio clock reference.
2. Gửi `HPD0` với empty clock reference.
3. Trả lời `SYNC STOP`.
4. Chờ hai `ASYN RELS` tối đa 400 ms.
5. Gửi `HPD0` lần cuối, release interface, tắt QuickTime configuration và quay lại usbmux configuration.

Protocol được cô lập trong `src/capture/usb/QuickTimeProtocol.*` để có thể thay thế khi Apple thay đổi iPadOS.
