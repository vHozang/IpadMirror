# PadMirror

PadMirror là ứng dụng mirror màn hình iPad dành cho chơi game, bám theo `PadMirror_USB_Gaming_Architecture.md`:

- USB là đường chính: QuickTime-style USB capture, H.264 hardware decode, video queue 1 frame.
- Audio 48 kHz đi độc lập với video, buffer mặc định 10 ms và tự quay về live edge khi vượt 40 ms.
- Windows 10 x64 dùng D3D11 + WASAPI; macOS dùng VideoToolbox + CoreAudio.
- Cùng một mạng Wi-Fi có thể mirror qua AirPlay bằng UxPlay chạy như một tiến trình GPLv3 riêng.

Không có recording, screenshot, OBS output, filter, HDR, upscale, remote control, multi-device hay mục tiêu 120 FPS trong V1.

## Build Windows 10 x64

Cần Windows 10/11 x64, PowerShell, Python 3 và kết nối Internet. Script tự tải toolchain MSVC portable, Qt, CMake/Ninja, GStreamer, libusb và Inno Setup vào phạm vi user, không cần cài Visual Studio hay vcpkg thủ công.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Kết quả:

```text
build\windows-x64\stage\PadMirror.exe
dist\PadMirror-portable.zip
dist\PadMirrorSetup.exe
```

Installer và bản portable đã kèm Qt, GStreamer, libusb, MSVC runtime và UxPlay. Khi khởi động, PadMirror kiểm tra các media component bắt buộc; nếu file runtime bị thiếu/hỏng, ứng dụng chạy `dependencies\repair-runtime.ps1`, tải bộ GStreamer chính thức, kiểm tra SHA-256 rồi cài lại tự động. USB Gaming cũng tự kiểm tra và cài driver lọc UsbDk có chữ ký Red Hat qua UAC mà không thay driver Apple.

Chi tiết: [docs/build.md](docs/build.md) và [docs/usb-setup.md](docs/usb-setup.md).

## Chạy USB Gaming

1. Kết nối iPad bằng cáp có truyền dữ liệu.
2. Mở khóa iPad và chọn **Trust This Computer** nếu được hỏi.
3. Mở PadMirror, chọn `USB Gaming`; ứng dụng tự dò và tự reconnect.
4. Chọn DAC/IEM, để buffer `10 ms`, `Gaming Mode` bật và `Strict sync` tắt.

Windows cần UsbDk để `libusb` truy cập iPad song song với driver Apple. PadMirror tự kiểm tra và mở bộ cài đã xác minh chữ ký/SHA-256 trong lần chạy USB đầu tiên.

## Chạy cùng Wi-Fi

1. Đặt iPad và máy tính trong cùng mạng Wi-Fi, tắt AP/client isolation.
2. Trong Settings, chọn `AirPlay Wi-Fi`; bản release tự nhận `uxplay\uxplay.exe` đã đóng gói.
3. Chấp nhận UAC để PadMirror tự tạo rule Windows Firewall cho `uxplay.exe`.
4. Trên iPad: Control Center -> Screen Mirroring -> `PadMirror`.

Chi tiết: [docs/wifi.md](docs/wifi.md). Wi-Fi là đường dự phòng; độ trễ và FPS phụ thuộc mạng, không thay thế USB Gaming.

## Kiểm tra

Core tests:

```bash
cmake -S . -B build/core -DPADMIRROR_BUILD_APP=OFF
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Core tests bao phủ packet parser, protocol replies, audio ring buffer và live-edge policy. Trong quá trình validation, parser/handshake cũng đã được đối chiếu với fixture thực tế của `ios-screen-record`. Việc tương thích iPad/iPadOS, latency thật và phiên PUBG 60-120 phút vẫn phải xác nhận trên phần cứng đích.

Tài liệu kỹ thuật: [kiến trúc](docs/architecture.md), [USB protocol](docs/usb-protocol-notes.md), [đo latency](docs/latency-testing.md).
