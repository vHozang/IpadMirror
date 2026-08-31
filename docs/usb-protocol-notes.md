# QuickTime protocol notes

File này ghi lại phần **protocol và parser** QuickTime đang được triển khai, độc lập
với transport. Nó chỉ mô tả state machine, packet parsing và fixture để test; **không**
mô tả device control, configuration switching, interface claim, bulk-endpoint binding
hay bất kỳ thao tác driver nào. Việc mở kênh AV do Apple runtime đảm nhận (xem
`docs/usb-setup.md`); nếu không có kênh an toàn, USB báo `USB_CAPTURE_UNSUPPORTED`.

`QuickTimeProtocol` và `PacketParser` được cô lập trong `src/capture/usb/` để có thể
thay thế khi Apple thay đổi iPadOS, và để tái sử dụng như một bộ parser thuần khi
chuyển output sang packet contract dùng chung (`EncodedVideoPacket`/`EncodedAudioPacket`).

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

## Media parsing

- `ASYN FEED` chứa video `CMSampleBuffer`.
- `ASYN EAT!` chứa audio `CMSampleBuffer`.
- H.264 AVCC được đổi sang Annex B; SPS/PPS được chèn khi format description thay đổi.
- Audio hiện hỗ trợ Linear PCM S16LE, mặc định 48 kHz stereo.
- Sau mỗi video `FEED`, PadMirror gửi lại `NEED` để giữ stream chạy.
- Thay đổi format description (video hoặc audio) và bước nhảy PTS được parser phát hiện
  và ánh xạ sang discontinuity của packet contract.

## Shutdown

1. Gửi `HPA0` với device audio clock reference.
2. Gửi `HPD0` với empty clock reference.
3. Trả lời `SYNC STOP`.
4. Chờ hai `ASYN RELS` tối đa 400 ms.
5. Gửi `HPD0` lần cuối và kết thúc session.

## Test fixtures

Các fixture protocol dùng để kiểm tra thuần parser, không cần thiết bị thật:

- Packet length/caps bounds.
- AVCC -> Annex B, thay đổi SPS/PPS.
- Handshake, clock reply, `NEED`, và close/release ordering.
- Ánh xạ format/PTS change sang discontinuity reason.
