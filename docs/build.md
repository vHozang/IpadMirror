# Build PadMirror

## Windows 10 x64 - mục tiêu chính

### Phụ thuộc

- Windows 10/11 x64 và PowerShell 5.1+.
- Python 3 trong `PATH`.
- Internet trong lần provision đầu tiên.

`scripts\install-build-dependencies.ps1` tự provision MSVC 2022 portable, Qt 6.8.3, CMake/Ninja, GStreamer MSVC 1.28.5, libusb 1.0.30 và Inno Setup dưới `D:\PadMirrorTools`/`LocalAppData`. Không trộn Qt/GStreamer MinGW vào binary MSVC của PadMirror. UxPlay dùng bản MSYS2 UCRT64 vì chạy ở tiến trình riêng.

### Kiểm tra GStreamer

```powershell
$env:PATH = "C:\gstreamer\1.0\msvc_x86_64\bin;$env:PATH"

gst-inspect-1.0 d3d11h264dec
gst-inspect-1.0 d3d11videosink
gst-inspect-1.0 wasapi2sink
```

Ba element trên cần xuất hiện. `wasapisink` được dùng làm fallback nếu máy không có `wasapi2sink`.

### Build tự động

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Kết quả:

```text
build\windows-x64\stage\PadMirror.exe
dist\PadMirror-portable\PadMirror.exe
dist\PadMirror-portable.zip
dist\PadMirrorSetup.exe
```

Script chạy toàn bộ test, `windeployqt`, đóng gói runtime và gọi `PadMirrorRuntimeCheck.exe`. Build dừng ngay nếu thiếu H.264 parser/depayloader, D3D11 decoder/sink hoặc WASAPI sink.

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
- USB Gaming tự kiểm tra UsbDk. Nếu thiếu, ứng dụng chạy bộ cài Red Hat đã được kiểm tra chữ ký và SHA-256 qua cửa sổ UAC, không thay driver Apple.
- AirPlay Wi-Fi tự tạo rule Windows Firewall cho UxPlay và bind mDNS vào card LAN vật lý đang dùng.
- UxPlay 1.74 và source GPLv3 được đóng gói dưới `uxplay\`; ứng dụng tự phát hiện đường dẫn này.

`libimobiledevice` mặc định tắt trong script Windows để đường build chính ít phụ thuộc. Nếu đã có bộ thư viện MSVC/pkg-config tương thích, dùng:

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
  -DPADMIRROR_LIBUSB_ROOT="$env:PADMIRROR_LIBUSB_ROOT" `
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
