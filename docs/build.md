# Build PadMirror

## Windows 10/11 x64 - mục tiêu duy nhất

PadMirror V1 chỉ hỗ trợ Windows 10/11 x64. Không có target macOS hay Linux.

### Phụ thuộc

- Windows 10/11 x64 và PowerShell 5.1+.
- Internet trong lần provision đầu tiên.

`scripts\build-windows.ps1` tự provision MSVC 2022 portable, Qt 6.8.3, CMake/Ninja, GStreamer MSVC 1.28.5 và Inno Setup dưới `D:\PadMirrorTools`/`LocalAppData`. USB Windows link SetupAPI + WinUSB từ Windows SDK và chỉ dùng kênh AV user-mode mà Apple runtime đã expose; không cài, thay thế, rebind hay filter driver. Không trộn Qt/GStreamer MinGW vào binary MSVC của PadMirror.

### Kiểm tra GStreamer

```powershell
$env:PATH = "C:\gstreamer\1.0\msvc_x86_64\bin;$env:PATH"

gst-inspect-1.0 d3d11h264dec
gst-inspect-1.0 d3d11h265dec
gst-inspect-1.0 avdec_aac
gst-inspect-1.0 d3d11videosink
gst-inspect-1.0 wasapi2sink
```

H.264 phục vụ QuickTime USB; H.265 phục vụ nội dung HEVC. HEVC có thêm fallback NVIDIA/libav và `wasapisink` được dùng khi máy không có `wasapi2sink`. Media stack là GStreamer + D3D11 hardware decode/render + WASAPI.

### Build tự động

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Kết quả:

```text
build\windows-x64\stage\PadMirror.exe
dist\PadMirror-portable.zip
dist\PadMirrorSetup.exe
```

Script chạy toàn bộ test, build backend QuickTime/WinUSB, `windeployqt`, đóng gói runtime và gọi `PadMirrorRuntimeCheck.exe`. Build dừng nếu thiếu pipeline H.264, D3D11 sink hoặc WASAPI sink.

Bước đóng gói chạy một scan bắt buộc và fail nếu phát hiện bất kỳ artifact bị cấm nào: `uxplay.exe` hoặc bundle UxPlay độc lập, UxPlay build bằng MSYS2, artifact OBS, Python USB bridge, `libusb*`/UsbDk, script cài/gỡ driver, helper WinUSB, hoặc plugin GStreamer chỉ phục vụ RTP relay (`gstudp`/`gstrtpmanager`/`gstrtp`).

### Runtime trên máy người dùng

- Installer kiểm tra registry của Microsoft Visual C++ x64 Runtime và chạy `dependencies\VC_redist.x64.exe` nếu thiếu.
- Bản portable có sẵn các DLL MSVC cần thiết nên vẫn khởi động được trước khi VC Redistributable được cài hệ thống.
- Ứng dụng kiểm tra GStreamer khi khởi động. Nếu package thiếu/hỏng, `repair-runtime.ps1` tải installer chính thức, xác minh SHA-256, cài theo user rồi chép lại các DLL/plugin bắt buộc.
- USB Gaming tự kiểm tra Apple Devices / Apple Mobile Device Service và interface QuickTime `MI_02`. App có thể cài Apple Devices chính chủ qua Microsoft Store/winget. Nếu không mở được kênh AV an toàn bằng Apple stack hiện có, USB báo `USB_CAPTURE_UNSUPPORTED` và dừng sạch; không có UsbDk/libusb0 cleanup hay remediation driver qua UAC.
- AirPlay Wi-Fi (in-process) hiện chưa có; xem `docs/wifi.md`.

`libimobiledevice` native mặc định tắt trong script Windows vì capture dùng SetupAPI/WinUSB trực tiếp. Nếu đã có bộ thư viện MSVC/pkg-config tương thích cho metadata/trust, dùng:

```powershell
.\scripts\build-windows.ps1 -EnableIMobileDevice
```

### Build thủ công với dependency có sẵn

```powershell
$env:PKG_CONFIG_PATH = "$env:GSTREAMER_ROOT_X86_64\lib\pkgconfig;$env:GSTREAMER_ROOT_X86_64\share\pkgconfig"
$env:PATH = "$env:GSTREAMER_ROOT_X86_64\bin;$env:PATH"

cmake -S . -B build\windows-x64 `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:QTDIR" `
  -DPADMIRROR_ENABLE_IMOBILEDEVICE=OFF

cmake --build build\windows-x64 --parallel
ctest --test-dir build\windows-x64 --output-on-failure
```

## Core tests không cần Qt/GStreamer

Core tests là portable (chạy được trên Windows/Linux/macOS, không cần Qt hay GStreamer):

```bash
cmake -S . -B build/core \
  -DPADMIRROR_BUILD_APP=OFF \
  -DPADMIRROR_BUILD_TESTS=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```
