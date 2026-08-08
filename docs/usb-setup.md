# Thiết lập USB

## Windows 10/11 x64

PadMirror chỉ dùng stack USB chính chủ của Apple: Apple Devices, Apple Mobile Device Service, pairing/trust và RemoteXPC user-space. Bản Windows không mở iPad bằng raw `libusb` và không cần kernel capture driver.

Apple chỉ cho phép `DisplayService` truyền video USB từ iPadOS 27. Nếu iPad đang chạy iPadOS 26, PadMirror tự chuyển sang receiver AirPlay Wi-Fi trên cùng mạng; ứng dụng không hạ cấp về UsbDk để tránh tái diễn màn hình xanh.

1. Cài **Apple Devices** từ Microsoft Store.
2. Cắm iPad bằng cáp data, mở khóa và chọn **Trust This Computer**.
3. Trên iPad, bật `Settings -> Privacy & Security -> Developer Mode`, restart iPad và xác nhận bật Developer Mode.
4. Giữ `Apple Mobile Device USB Composite Device` dùng driver Apple mặc định.
5. Mở PadMirror và chọn `USB Gaming`.

Lần chạy đầu, PadMirror kiểm tra Apple Mobile Device Service và USB bridge đi kèm. Nếu Apple Devices còn thiếu, app dùng `winget` để mở luồng cài chính chủ. Bridge tự tải/mount Developer Disk Image phù hợp với iPadOS khi bắt đầu capture.

## Bảo vệ khỏi WDF_VIOLATION

Không cài UsbDk, Zadig hoặc `libusb-win32/libusb0` lên iPad. Các filter/driver này không còn được PadMirror sử dụng và có thể gây `WDF_VIOLATION` trên một số máy Windows.

Nếu phát hiện bản cũ, PadMirror chạy `dependencies\remove-unsafe-usb-filter.ps1` qua UAC để:

- Vô hiệu hóa và gỡ UsbDk.
- Gỡ filter `libusb0` khỏi thiết bị Apple mà không thay driver Apple.
- Yêu cầu restart Windows một lần nếu driver cũ đang nằm trong kernel.

Không thử lại USB trước khi hoàn tất lần restart đó.

## Trình tự chạy

1. Cắm iPad trực tiếp vào máy, tránh hub trong lần thử đầu.
2. Mở khóa iPad; đóng 3uAirPlayer hoặc app mirror khác.
3. Chọn `USB Gaming` trong PadMirror.
4. Chờ trạng thái `Preparing Apple's Developer Disk Image`, rồi `USB Gaming Mode active`.
5. Nếu rút cáp, cắm lại và mở khóa iPad để app kết nối lại.

Log Windows: `%LOCALAPPDATA%\PadMirror\PadMirror\logs\padmirror.log`.

## Lỗi thường gặp

| Thông báo | Cách xử lý |
| --- | --- |
| No iPad was found through Apple Devices | Đổi cáp/cổng, mở khóa iPad, kiểm tra Apple Devices có nhìn thấy máy |
| Trust This Computer | Chọn Trust trên iPad, nhập passcode rồi rút/cắm lại cáp |
| Enable Developer Mode | Bật Developer Mode, restart iPad và xác nhận sau khi máy lên |
| Developer Disk Image setup failed | Kiểm tra Internet, ngày giờ Windows và thử lại |
| Apple DisplayService is unavailable | Xác nhận Developer Mode đã bật, rút/cắm lại iPad |
| Safe USB video requires iPadOS 27 | App tự chuyển sang AirPlay Wi-Fi; chọn `PadMirror` trong Screen Mirroring |
| UsbDk was disabled and is pending removal | Restart Windows một lần rồi mở lại PadMirror |
| HEVC decoder is unavailable | Cập nhật driver GPU hoặc chạy lại installer để repair GStreamer |
| Audio device unavailable | Chọn lại DAC, rút/cắm DAC rồi restart receiver |
