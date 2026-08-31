# Mirror khi cùng Wi-Fi

Wi-Fi dùng AirPlay và là đường dự phòng của USB. USB vẫn là lựa chọn cho PUBG vì ít jitter và ít tích lũy latency hơn.

## Trạng thái hiện tại

AirPlay Wi-Fi hiện **chưa khả dụng**. Receiver AirPlay được port trực tiếp vào `PadMirror.exe` (cùng process, dựa trên mã UxPlay GPLv3), không chạy tiến trình `uxplay.exe` ngoài và không dùng RTP relay qua loopback. Phần native này được lên lịch cho các mốc M6/M7 và chưa hoàn thiện, nên PadMirror báo AirPlay ở trạng thái `Unavailable` cho tới khi receiver native được vendored vào binary.

Tới thời điểm đó, hãy dùng USB Gaming (`docs/usb-setup.md`).

## Yêu cầu mạng (khi AirPlay khả dụng)

- iPad và máy tính ở cùng LAN/VLAN.
- Tắt `AP isolation`, `client isolation` hoặc guest-network isolation.
- Windows network profile nên là `Private`.
- Hai thiết bị không ở guest Wi-Fi.

Receiver dùng bộ port AirPlay cố định tương thích UxPlay và Bonjour/mDNS in-process; không có RTP relay nội bộ và không mở cổng relay ra LAN.

## Hành vi dự kiến

Khi native receiver hoàn thiện, PadMirror sẽ đăng ký dịch vụ AirPlay qua Bonjour (`dns_sd`) ngay trong process với tên receiver `PadMirror-PC`. Nếu Bonjour không có trên máy, AirPlay chuyển sang `Unavailable`.

1. Mở PadMirror -> Settings.
2. Connection -> `AirPlay Wi-Fi`.
3. Trên iPad mở Control Center -> Screen Mirroring -> `PadMirror-PC`.

Video H.264/HEVC và audio L16 được đưa thẳng vào cùng `MediaEngine` (GStreamer + D3D11 + WASAPI) mà USB dùng. Khi USB đang streaming, kết nối AirPlay bị từ chối với trạng thái `BUSY`; cắm USB trong lúc AirPlay đang chạy chỉ hiển thị `USB AVAILABLE` chứ không tự ngắt AirPlay.

## Firewall

Hiện không có script firewall nào được đóng gói. Nếu về sau cần cấu hình firewall, rule sẽ được scope vào `PadMirror.exe` (sau M6/M7), không phải cho `uxplay.exe`.

## Licensing

Sản phẩm giữ license hiện tại (MIT) cho tới khi mã native dẫn xuất từ UxPlay thực sự được vendored vào binary. Tại thời điểm gated đó, license chuyển sang GPL-3.0-or-later kèm complete corresponding source. Hiện tại chưa nhúng mã UxPlay và không có source archive GPL nào được ship.
