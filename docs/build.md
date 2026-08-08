# Build PadMirror

## Windows 10 x64 - mục tiêu chính

### Phụ thuộc

- Windows 10/11 x64 và PowerShell 5.1+.
- Python 3 trong `PATH`.
- Internet trong lần provision đầu tiên.

`scripts\install-build-dependencies.ps1` tự provision MSVC 2022 portable, Qt 6.8.3, CMake/Ninja, GStreamer MSVC 1.28.5 và Inno Setup dưới `D:\PadMirrorTools`/`LocalAppData`. `build-usb-bridge.ps1` dùng Python bootstrap để provision Python 3.12, `pymobiledevice3` và PyInstaller cho bridge user-space. Không trộn Qt/GStreamer MinGW vào binary MSVC của PadMirror. UxPlay dùng bản MSYS2 UCRT64 vì chạy ở tiến trình riêng.

### Kiểm tra GStreamer

```powershell
$env:PATH = "C:\gstreamer\1.0\msvc_x86_64\bin;$env:PATH"

gst-inspect-1.0 d3d11h264dec
gst-inspect-1.0 d3d11h265dec
gst-inspect-1.0 avdec_aac
gst-inspect-1.0 d3d11videosink
gst-inspect-1.0 wasapi2sink
```

H.264 phục vụ AirPlay; H.265/AAC phục vụ USB Windows. HEVC có thêm fallback NVIDIA/libav và `wasapisink` được dùng khi máy không có `wasapi2sink`.

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

Script chạy toàn bộ test, build/probe USB bridge, `windeployqt`, đóng gói runtime và gọi `PadMirrorRuntimeCheck.exe`. Build dừng nếu thiếu H.264/H.265 parser, HEVC/AAC decoder, D3D11 sink hoặc WASAPI sink.

UxPlay được lấy từ `-UxPlayRoot`, sau đó từ `D:\PadMirrorTools\uxplay-bundle` hoặc `third_party\uxplay-windows`. Bundle phát hành phải có `uxplay.exe`, DLL/plugin GStreamer MinGW, GPLv3 và source archive tương ứng.

Tạo bundle đã patch trong MSYS2 UCRT64 trước khi build release:

```bash
./scripts/build-uxplay-windows.sh
```

Script ghim đúng revision UxPlay 1.74, áp dụng `third_party/uxplay/PadMirror-mDNS-interface.patch`, đóng gói dependency riêng và tạo source archive tương ứng.

### Runtime trên máy người dùng

- Installer kiểm tra registry của Microsoft Visual C++ x64 Runtime và chạy `dependencies\VC_redist.x64.exe` nếu thiếu.
- Bản portable có sẵn các DLL MSVC cần thiết nên vẫn khởi động được trước khi VC Redistributable được cài hệ thống.
- Ứng dụng kiểm tra GStreamer khi khởi động. Nếu package thiếu/hỏng, `repair-runtime.ps1` tải installer chính thức, xác minh SHA-256, cài theo user rồi chép lại các DLL/plugin bắt buộc.
- USB Gaming tự kiểm tra Apple Devices, Apple Mobile Device Service và bridge user-space đi kèm. App có thể cài Apple Devices chính chủ qua Microsoft Store/winget.
- Nếu phát hiện UsbDk hoặc filter `libusb0` từ bản cũ, app chỉ gỡ chúng qua UAC và yêu cầu restart; bản mới không cài hay sử dụng các driver đó.
- AirPlay Wi-Fi tự tạo rule Windows Firewall cho UxPlay và bind mDNS vào card LAN vật lý đang dùng.
- UxPlay 1.74 và source GPLv3 được đóng gói dưới `uxplay\`; ứng dụng tự phát hiện đường dẫn này.

`libimobiledevice` native mặc định tắt trong script Windows vì USB bridge đã đóng gói phần Apple user-space cần thiết. Nếu đã có bộ thư viện MSVC/pkg-config tương thích, dùng:

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

## macOS

Mục tiêu đầu tiên là Apple Silicon. Cài Qt, GStreamer, libusb và libimobiledevice bằng Homebrew:

```bash
brew install cmake ninja qt gstreamer libusb libimobiledevice

cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build/macos
ctest --test-dir build/macos --output-on-failure
```

Direct USB claim trên macOS có thể cần quyền/entitlement phù hợp và phải được kiểm tra trên máy thật. VideoToolbox/CoreAudio translation units cần được xác nhận bằng Apple SDK; Linux chỉ kiểm tra được phần C++ dùng chung.

## Core tests không cần Qt/GStreamer

```bash
cmake -S . -B build/core \
  -DPADMIRROR_BUILD_APP=OFF \
  -DPADMIRROR_BUILD_TESTS=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```
