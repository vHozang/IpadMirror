# Thiết lập USB

## Driver USB trên Windows

1. Cài Apple Devices hoặc iTunes chính thức.
2. Kết nối iPad, mở khóa và hoàn tất **Trust This Computer**.
3. Kiểm tra cáp có truyền dữ liệu, không chỉ sạc.
4. Giữ nguyên driver Apple trong Device Manager.

PadMirror dùng `libusb` với backend UsbDk để gửi control request bật QuickTime configuration và claim interface capture. UsbDk là filter driver có chữ ký Red Hat, hoạt động cùng driver Apple thay vì thay thế driver composite của iPad.

Trong lần chạy USB đầu tiên, ứng dụng:

- Kiểm tra service `UsbDk`.
- Xác minh SHA-256 và chữ ký Red Hat của MSI đi kèm.
- Mở UAC và cài UsbDk nếu còn thiếu.
- Giữ giao diện PadMirror hoạt động trong lúc chờ UAC.

Không dùng Zadig để thay driver `Apple Mobile Device USB Composite Device`. Thao tác đó có thể làm Apple Devices, iTunes, pairing và Apple Mobile Device Service ngừng nhận iPad.

Không có một lựa chọn Zadig duy nhất an toàn cho mọi máy. Hãy thử trên đúng máy/iPad mục tiêu và giữ khả năng rollback.

## Trình tự chạy

1. Cắm iPad trực tiếp vào máy, tránh hub trong lần thử đầu.
2. Mở khóa iPad.
3. Chạy PadMirror và chọn `USB Gaming`.
4. Theo dõi Diagnostics và log:
   - Windows: `%LOCALAPPDATA%\PadMirror\PadMirror\logs\padmirror.log`
   - macOS: thư mục AppLocalData của PadMirror, file `logs/padmirror.log`.
5. Nếu rút cáp, ứng dụng chuyển sang reconnect và tự kết nối lại khi iPad xuất hiện.

## Lỗi thường gặp

| Thông báo | Kiểm tra |
| --- | --- |
| No iPad was found on USB | Cáp data, cổng USB, iPad đã mở khóa, driver có nhìn thấy thiết bị |
| Access was denied | UsbDk chưa được cài, UAC bị hủy hoặc cần restart Windows một lần |
| Cannot claim QuickTime interface | Rút/cắm lại cáp sau khi cài UsbDk, rồi thử restart Windows |
| iPad did not reconnect with QuickTime interface | Đổi cáp/cổng, chờ iPad reconnect, đóng phần mềm Apple đang truy cập thiết bị |
| Hardware H.264 decoder is unavailable | Cài đúng GStreamer MSVC plugin và cập nhật driver GPU |
| Audio device unavailable | Chọn lại DAC, rút/cắm DAC rồi restart receiver |
