# Đo latency và độ ổn định AirPlay

Các test dưới đây áp dụng cho iPad → AirPlay LAN → PadMirror trên Windows. Unit tests và runtime checker không chứng minh latency hoặc tương thích iPad thực tế.

## Ghi lại cấu hình test

Mỗi kết quả cần kèm:

- Model iPad và version iPadOS.
- AP/router, band 5/6 GHz, channel width và topology (PC Ethernet hay Wi-Fi).
- Windows version, GPU/driver, monitor refresh rate.
- PadMirror version/commit, setting 30 hoặc 60 FPS.
- GStreamer version và audio output device.
- Codec/resolution/sample rate hiển thị trong Diagnostics.

Không so sánh các lần đo có topology hoặc display settings khác nhau như cùng một baseline.

## Motion-to-photon video latency

Phương pháp ưu tiên:

1. Hiển thị visual event có cạnh thời gian rõ trên iPad (timer đổi màu, input flash hoặc game action lặp lại).
2. Dùng camera high-speed quay đồng thời màn hình iPad và monitor PC.
3. Với nhiều event, đếm số frame từ thay đổi trên iPad tới thay đổi tương ứng trên monitor.
4. Quy đổi:

   ```text
   latency_ms = frame_difference / camera_fps × 1000
   ```

5. Báo median, p95, minimum/maximum và số sample; không chỉ báo một lần tốt nhất.

Đo display-to-display bao gồm scanout/camera phase của cả hai màn hình. Nó phù hợp đánh giá trải nghiệm end-to-end nhưng không tách riêng network/decode/render.

Các mốc tham khảo phát triển, không phải cam kết sản phẩm:

- < 100 ms: tốt cho AirPlay mirroring.
- 50–80 ms ổn định: rất tốt nếu không đổi bằng frame corruption hoặc audio lỗi.

## Frame rate và video quality

Setting 30/60 FPS là giá trị receiver quảng bá, không tạo frame mới. Kiểm tra bằng clip/game có motion liên tục:

- iPad thật sự gửi 30 hay 60 FPS.
- H.264 và H.265 khi client lựa chọn được từng codec.
- Không tearing/corruption sau chuyển app, seek hoặc packet burst.
- Không tích lũy latency theo thời gian.
- Aspect ratio đúng khi portrait/landscape, resize và fullscreen.
- Black level/color range không bị washed-out hoặc crushed.

Video pipeline không leaky-drop encoded access units. Queue ba frame, downstream-leaky chỉ nằm sau decode/conversion để ưu tiên live edge an toàn.

## Audio latency và chất lượng

Dùng sound cue đi cùng visual event rõ, ghi đồng thời monitor và output speaker/DAC:

- Đo thời điểm audio onset so với event trên iPad.
- Đo audio so với frame tương ứng trên monitor để theo dõi lip-sync.
- Nghe bằng đoạn game dài, silence xen burst và âm cao để phát hiện screech/click.
- Xác nhận AAC-ELD sample rate/channel trong Diagnostics.

Đặc biệt kiểm tra:

- Không im tiếng ở phiên đầu hoặc sau reconnect.
- Không phát burst nhanh sau connect/app switch.
- Không tích lũy audio delay.
- Volume/mute thay đổi an toàn khi đang stream.
- Đổi Windows output device hoặc sleep/wake có recovery rõ ràng.

## Discontinuity và recovery matrix

Mỗi case phải quan sát cả hình, tiếng, state và logs:

1. Connect → disconnect → reconnect.
2. Chuyển app iPad trong lúc mirror.
3. Seek video tiến/lùi.
4. Portrait ↔ landscape nhiều lần.
5. Lock/unlock iPad nếu iPadOS cho phép phiên tiếp tục/reconnect.
6. Stop/start receiver từ toolbar.
7. Thay receiver name hoặc 30/60 FPS để buộc controlled restart.
8. Enter/exit fullscreen và resize nhanh.
9. Windows sleep/wake.
10. Đóng PadMirror khi đang streaming.

Pass khi không crash/deadlock, receiver có thể trở lại Ready, frame/audio cũ không phát lại sau flush và phiên mới không tăng latency bất thường.

## Long-session soak

Chạy ít nhất 30, 60 và 120 phút. Theo dõi:

- A/V drift theo thời gian.
- Latency đầu phiên so với cuối phiên.
- Frame corruption/stall kéo dài.
- Audio silence, screech, burst hoặc underrun nghe thấy.
- Working set/handle count có tăng không giới hạn.
- GPU utilization/temperature và thermal throttling.
- Network loss/jitter khi AP có tải thực tế.
- `padmirror.log` và `gstreamer.log` có warning/error lặp lại.

Lưu log cùng timestamp của sự cố; log rotate ở 4 MiB nên sao chép cả file hiện tại và `.1` ngay sau test dài.

## Tiêu chí release hardware

Không coi V1 được xác nhận end-to-end chỉ vì build/tests/runtime checker pass. Release hardware validation cần tối thiểu:

- Discovery nhanh trên mạng Private không isolation.
- Connect/reconnect ổn định.
- H.264 và H.265 render bằng D3D11.
- AAC-ELD rõ, không silent/screech/accumulate delay.
- 30/60 FPS advertisement hoạt động đúng.
- Orientation, seek/app switch, fullscreen/resize recovery.
- Phiên dài không A/V drift hoặc resource leak rõ ràng.
- Firewall vẫn không bị thay đổi khi người dùng chưa chọn/call helper.
