# Thiết lập USB

## Windows 10/11 x64

PadMirror giữ **nguyên** stack USB chính chủ của Apple (composite, pairing, usbmux).
App **không** cài, thay thế, rebind hay filter driver Apple: không rebind `MI_02`
sang WinUSB, không libusb, UsbDk, Zadig, không Python bridge, không Developer Mode
hoặc Developer Disk Image. USB capture chỉ chạy khi Apple runtime tự cung cấp một
kênh AV user-mode an toàn.

1. Cài **Apple Devices** (hoặc Apple Mobile Device Support) từ Microsoft Store.
2. Cắm iPad bằng cáp data, mở khóa và chọn **Trust This Computer** (nhập passcode nếu
   iPad yêu cầu).
3. Mở PadMirror và chọn `USB Gaming`.

Khi bắt đầu, PadMirror chạy một **capability probe không xâm lấn**: kiểm tra Apple
runtime/AMDS, trạng thái Trust qua Lockdown handshake, và hỏi xem Apple stack có expose
sẵn kênh AV user-mode tương thích hay không. Probe này **chỉ đọc**, không gọi installer
INF, không `WinUsb_Initialize`, không `SetupDiInstallDevice`, không đổi configuration
hay claim interface.

## Khi không có kênh an toàn (USB_CAPTURE_UNSUPPORTED)

Nếu cách duy nhất để lấy interface QuickTime là thay driver Apple, PadMirror **không**
làm điều đó. Backend báo `USB_CAPTURE_UNSUPPORTED` và dừng sạch, không để lại thay đổi
nào trên thiết bị hoặc driver. UI hiển thị lý do và, nếu AirPlay khả dụng, gợi ý người
dùng chuyển sang AirPlay. USB capable hay không là một go/no-go gate, không có fallback
"cố làm cho chạy" bằng driver thay thế.

## Trình tự chạy

1. Cắm iPad trực tiếp vào máy, tránh hub trong lần thử đầu.
2. Mở khóa iPad và Trust This Computer.
3. Chọn `USB Gaming` trong PadMirror; khi thiết bị trusted xuất hiện và app đang ở USB
   mode, capture tự start.
4. Chờ trạng thái streaming; hình và tiếng xuất hiện tự động.
5. Nếu rút cáp, cắm lại và mở khóa iPad; backend tạo session mới với epoch mới, không
   cần restart app.

Log Windows: `%LOCALAPPDATA%\PadMirror\PadMirror\logs\padmirror.log`.

## Lỗi thường gặp

| Thông báo | Cách xử lý |
| --- | --- |
| No iPad was found | Đổi cáp/cổng, mở khóa iPad, kiểm tra Apple Devices/AMDS có nhìn thấy máy |
| Trust This Computer | Chọn Trust trên iPad, nhập passcode rồi rút/cắm lại cáp |
| USB_CAPTURE_UNSUPPORTED | Apple runtime không mở được kênh AV an toàn trên máy/iPadOS này; dùng AirPlay khi khả dụng. PadMirror không thay driver để ép chạy |
| Apple runtime missing | Cài/mở lại Apple Devices (AMDS), rồi thử lại USB |
| Audio device unavailable | Chọn lại DAC, rút/cắm DAC rồi thử lại |
