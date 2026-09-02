# PadMirror

PadMirror là ứng dụng nhận **AirPlay Screen Mirroring từ iPad trên Windows 10/11 x64**, tối ưu cho hình ảnh và âm thanh trực tiếp có độ trễ thấp.

PadMirror chỉ có **một đường runtime**:

```text
iPad
  → AirPlay / RAOP trong mạng LAN
  → UxPlay native chạy trong PadMirror.exe
  → GStreamer
      • H.264/H.265 → D3D11 hardware decode/render
      • AAC-ELD/ALAC → libfdk-aac/GStreamer → WASAPI2
  → Qt Widgets UI
```

- Receiver UxPlay được biên dịch trực tiếp vào `PadMirror.exe`.
- Không chạy `uxplay.exe`, không RTP relay qua localhost và không cần OBS.
- Không có USB/QuickTime capture, driver/filter USB, Python bridge hoặc API HTTP cục bộ.
- UI dùng Qt Widgets; video được render vào HWND native của widget bằng `d3d11videosink`.
- PadMirror được phân phối theo **GPL-3.0-or-later** vì liên kết mã có nguồn gốc UxPlay GPLv3.

> AirPlay yêu cầu người dùng chọn receiver trên iPad. PadMirror không thể tự khởi tạo phiên Screen Mirroring thay cho iPad.

## Yêu cầu runtime

- Windows 10 hoặc Windows 11, x64.
- GPU/driver hỗ trợ GStreamer D3D11 H.264 và H.265 decoder.
- iPad và PC trong cùng LAN/VLAN, không bật AP/client isolation.
- Windows network profile nên là `Private`.
- Firewall cho phép `PadMirror.exe` nhận lưu lượng trên mạng Private/Domain.

Bản installer và portable đều tự chứa Qt, GStreamer/plugin cần thiết, MSVC runtime và `libfdk-aac-2.dll`. Ứng dụng kiểm tra runtime trước khi mở receiver; package không tải hoặc sửa runtime khi đang chạy.

## Chạy AirPlay

1. Mở PadMirror. Receiver mặc định tự chuyển từ **Starting** sang **Ready**.
2. Trên iPad, mở **Control Center → Screen Mirroring**.
3. Chọn tên receiver, mặc định là **PadMirror**.
4. Dùng **F11** hoặc double-click vùng video để bật/tắt fullscreen; nhấn **Escape** để thoát fullscreen.

Settings cho phép đổi tên receiver, quảng bá tối đa 30/60 FPS, volume/mute, keep-awake khi streaming, tự khởi động receiver và fullscreen khi kết nối. Những thay đổi ảnh hưởng quảng bá receiver được áp dụng bằng một lần restart có kiểm soát.

Nếu iPad không thấy receiver, xem [hướng dẫn mạng và firewall](docs/wifi.md). Firewall helper được đóng gói nhưng **không tự chạy** trong bản portable; trong installer đây là task bỏ chọn mặc định và chỉ chạy sau khi người dùng chọn rồi chấp nhận UAC. Rule chỉ áp dụng cho `PadMirror.exe` trên Private/Domain.

## Build Windows

Từ PowerShell ở thư mục repository:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Script provision toolchain khi cần, configure/build bằng MSVC, chạy tests, kiểm tra staged runtime, scan payload bị cấm và tạo:

```text
build\windows-x64\stage\PadMirror.exe
build\windows-x64\stage\PadMirrorRuntimeCheck.exe
dist\PadMirror-portable.zip
dist\PadMirrorSetup.exe
dist\SHA256SUMS.txt
```

Chi tiết dependency, build thủ công và runtime checker: [docs/build.md](docs/build.md).

## Tests portable

Hai test cấu hình/identity không phụ thuộc Qt hoặc GStreamer:

```bash
cmake -S . -B build/core -DPADMIRROR_BUILD_APP=OFF -DPADMIRROR_BUILD_TESTS=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Tests này kiểm tra normalize receiver settings và receiver identity/MAC ổn định; chúng không thay thế kiểm thử iPad end-to-end.

## Diagnostics và logs

**Diagnostics** hiển thị state, receiver/listener, client, codec/resolution, sample rate, GStreamer version và thư mục log.

Logs được rotate ở 4 MiB, giữ một bản `.1`, tại thư mục App Local Data của PadMirror:

- `padmirror.log`: log Qt/application.
- `gstreamer.log`: stderr của UxPlay/GStreamer native.

## Phạm vi V1

V1 không có recording, screenshot, filter, HDR, remote control, second display, multi-device, OBS integration hoặc mục tiêu 120 FPS. Mức 30/60 FPS là khả năng quảng bá; FPS thực tế do iPad, nội dung, mạng, decoder và display quyết định.

## Tài liệu

- [Kiến trúc](docs/architecture.md)
- [Build và packaging](docs/build.md)
- [Wi-Fi, discovery và firewall](docs/wifi.md)
- [Đo latency và độ ổn định](docs/latency-testing.md)
- [Third-party notices](LICENSES/THIRD_PARTY.md)
