# Kiểm tra latency và độ ổn định

## FPS

Bật Diagnostics và theo dõi:

- Source FPS.
- Decode FPS.
- Render FPS.
- Dropped frames.
- Audio buffer, underruns và resyncs.

Mục tiêu V1 là source/decode/render gần 60 FPS khi iPad thực sự gửi 60 FPS. Setting 90/120 FPS trong PUBG không đảm bảo capture USB vượt 60 FPS.

## Video latency

Dùng camera tốc độ cao quay đồng thời màn hình iPad và monitor. Chọn cùng một visual event rồi đếm frame giữa hai màn hình. Không đánh giá chỉ bằng cảm giác.

Mục tiêu phát triển:

- Dưới 100 ms: tốt.
- Khoảng 50-80 ms: rất tốt nếu ổn định.

Không coi đây là cam kết cố định; kết quả phụ thuộc iPad/iPadOS, USB controller, GPU, monitor và GStreamer.

## Audio latency

Tạo sound cue rõ trong game và ghi đồng thời sự kiện trên iPad với output DAC/IEM. Gaming Mode ưu tiên audio đến sớm, không cố thêm delay chỉ để lip-sync với video.

## Long-session

Chạy PUBG trong 30, 60 và 120 phút. Kiểm tra:

- Audio drift hoặc latency tăng dần.
- Buffer vượt 40 ms và số lần resync.
- Decoder stall, frame drop kéo dài.
- Rút/cắm lại USB.
- Memory leak và thermal throttling.
