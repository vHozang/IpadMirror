# PadMirror

PadMirror là ứng dụng mirror màn hình iPad sang PC, tối ưu cho chơi game độ trễ thấp. V1 chỉ hỗ trợ **Windows 10/11 x64**. Đường truyền chính là **AirPlay không dây**; USB chỉ khả dụng nếu Apple cung cấp kênh AV an toàn (thực tế trên Windows là không, nên USB báo unsupported một cách trung thực).

- **USB (Apple user-mode AV channel)**: chỉ hoạt động khi hệ thống Apple cung cấp một kênh AV ở user-mode được hỗ trợ. PadMirror **không bao giờ** cài, thay thế, rebind hay filter driver USB của Apple. Apple không cung cấp kênh AV như vậy trên Windows, nên USB báo `USB_CAPTURE_UNSUPPORTED` và dừng sạch.
- **AirPlay (in-process, GPLv3)**: receiver AirPlay chạy ngay trong `PadMirror.exe` bằng mã có nguồn gốc UxPlay được vendor vào `third_party/uxplay-native/`. Không chạy `uxplay.exe` ngoài tiến trình, không relay RTP qua loopback. Receiver bind listener RTSP/RAOP rồi mới publish record `_airplay._tcp`/`_raop._tcp`, nên iPad thấy PadMirror trong danh sách Screen Mirroring và kết nối trực tiếp. **Vì link mã GPLv3, license phân phối của PadMirror là GPL-3.0-or-later.**

> Kết nối AirPlay yêu cầu người dùng chọn PadMirror trong Control Center → Screen Mirroring trên iPad (đây là hành vi bắt buộc của giao thức AirPlay). PadMirror không thể tự khởi tạo phiên thay cho iPad.

Media stack chung: GStreamer + D3D11 hardware decode/render + WASAPI. Cả hai transport đẩy gói mã hoá vào một `MediaEngine` chung qua contract `IMediaIngress`; media không biết chi tiết transport.

Không có recording, screenshot, filter, HDR, remote control, multi-device, second display, OBS integration, cầu Python USB hay mục tiêu 120 FPS trong V1.

## Kiến trúc

```text
padmirror_core   (types, IMediaIngress, IMirrorBackend, MediaEngine, MirrorCoordinator)
|- padmirror_media   (VideoPipeline / AudioPipeline / MediaSession)
|- padmirror_usb     (AppleRuntimeProbe, AppleAvChannelFactory, UsbMirrorBackend)
|- padmirror_airplay (AirPlayBackend, BonjourPublisher)
`- padmirror_api     (loopback automation API)

padmirror_app -> tất cả module trên
```

`MirrorCoordinator` là owner duy nhất của session: bảo đảm USB XOR AirPlay, arm media pipeline trước khi backend chạy, flush khi chuyển mode, và map trạng thái backend sang UI/API. Chuyển USB ↔ AirPlay là thao tác explicit của người dùng.

## Build Windows 10/11 x64

Cần Windows 10/11 x64, PowerShell và kết nối Internet. Script tự provision MSVC portable, Qt, CMake/Ninja, GStreamer và Inno Setup.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Kết quả:

```text
build\windows-x64\stage\PadMirror.exe
dist\PadMirror-portable.zip
dist\PadMirrorSetup.exe
```

Bản release kèm Qt, GStreamer (chỉ các plugin decode/render/WASAPI cần thiết), MSVC runtime và license. Khi khởi động, PadMirror kiểm tra component bắt buộc; nếu runtime media thiếu/hỏng, có thể chạy `dependencies\repair-runtime.ps1` để tải lại GStreamer chính thức và kiểm tra SHA-256. Installer chạy ở quyền người dùng, chỉ cài Microsoft VC++ runtime khi thiếu; **không** đụng tới driver, service, filter hay firewall.

Chi tiết: [docs/build.md](docs/build.md) và [docs/usb-setup.md](docs/usb-setup.md).

## Chạy USB

1. Kết nối iPad bằng cáp có truyền dữ liệu.
2. Mở khoá iPad và chọn **Trust This Computer** nếu được hỏi.
3. Mở PadMirror, chọn `USB Gaming`. Khi một kênh AV user-mode của Apple khả dụng, PadMirror bắt đầu capture tự động; nếu không, ứng dụng hiển thị trạng thái `USB_CAPTURE_UNSUPPORTED` và gợi ý chuyển sang AirPlay (không tự chuyển).
4. Không cần Screen Mirroring, không cần cùng Wi-Fi, không cần Developer Mode.

PadMirror không cài UsbDk, Zadig hay `libusb`/`libusb0` cho iPad, và không thay/rebind/filter driver Apple trong bất kỳ trường hợp nào.

## Chạy AirPlay (khi khả dụng)

1. Đặt iPad và PC trong cùng mạng Wi-Fi, tắt AP/client isolation.
2. Trong Settings, chọn `AirPlay Wi-Fi`. Khi receiver in-process và Bonjour runtime sẵn sàng, PadMirror quảng bá `PadMirror-PC`.
3. Trên iPad: Control Center -> Screen Mirroring -> `PadMirror-PC`.

Receiver AirPlay native đã được tích hợp in-process (mã UxPlay vendored). Khi bật AirPlay, PadMirror bind listener rồi publish `_airplay._tcp`/`_raop._tcp`; nếu bind/đăng ký thất bại, backend báo lỗi trung thực thay vì quảng bá dịch vụ chết. Chi tiết: [docs/wifi.md](docs/wifi.md).

## Kiểm tra

Core tests (portable, không cần Qt/GStreamer):

```bash
cmake -S . -B build/core -DPADMIRROR_BUILD_APP=OFF -DPADMIRROR_BUILD_TESTS=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Core tests bao phủ packet parser, protocol replies, audio ring buffer, live-edge policy, media epoch/engine và `MirrorCoordinator` (USB XOR AirPlay, thứ tự arm/start, rollback khi lỗi). Tương thích iPad/iPadOS thực tế, latency, và phiên game dài phải xác nhận trên phần cứng đích sau khi M2/M6/M7 có triển khai thật.

Tài liệu kỹ thuật: [kiến trúc](docs/architecture.md), [USB protocol](docs/usb-protocol-notes.md), [đo latency](docs/latency-testing.md).
