# Build PadMirror

## Target hỗ trợ

Desktop application chỉ build và chạy trên **Windows 10/11 x64**. Tests của `padmirror_support` có thể build trên hệ điều hành khác khi tắt app.

## Build tự động trên Windows

Yêu cầu ban đầu:

- Windows PowerShell 5.1+.
- Python launcher/executable khả dụng cho provisioning.
- Internet khi dependency chưa được provision.

Từ repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

Mặc định script:

1. Chạy `scripts\install-build-dependencies.ps1`.
2. Provision/cache portable MSVC 2022, Qt 6.8.3 MSVC, CMake, Ninja, GStreamer MSVC 1.28.5 và Inno Setup.
3. Configure bằng CMake/Ninja với C11 và C++20.
4. Build `PadMirror.exe`, `PadMirrorRuntimeCheck.exe` và tests.
5. Chạy CTest.
6. Deploy Qt Widgets bằng `windeployqt` (không deploy Qt Quick/QML).
7. Stage GStreamer runtime/plugins, MSVC runtime, `libfdk-aac-2.dll`, license inventories, source archives, VC redistributable và firewall helper.
8. Chạy runtime checker trên chính stage.
9. Scan và fail nếu stage chứa payload legacy/bị cấm.
10. Tạo portable ZIP, Inno installer và SHA-256 manifest.

Artifacts:

```text
build\windows-x64\stage\PadMirror.exe
build\windows-x64\stage\PadMirrorRuntimeCheck.exe
dist\PadMirror-portable.zip
dist\PadMirrorSetup.exe
dist\SHA256SUMS.txt
```

Có thể bỏ bước Inno khi lặp build nhanh:

```powershell
.\scripts\build-windows.ps1 -SkipInstaller
```

`-InstallMissing:$false` bỏ provisioning và yêu cầu các root dependency đã tồn tại.

## Dependency và binary ABI

Build chính dùng một ABI thống nhất:

- MSVC 2022 x64.
- Qt 6.8.3 `msvc2022_64`, module Core/Gui/Widgets.
- GStreamer 1.28.5 MSVC x64.
- OpenSSL từ GStreamer MSVC bundle cho native AirPlay crypto.
- `libfdk-aac-2.dll` được load động cho AAC-ELD AOT-39.

Không trộn Qt/GStreamer MinGW vào targets MSVC. UxPlay-derived C code và vendored libplist được build trực tiếp bởi CMake vào `PadMirror.exe`; không build hoặc ship `uxplay.exe`.

Provision mặc định dùng/cache:

```text
D:\PadMirrorTools\msvc
D:\PadMirrorTools\Qt\6.8.3\msvc2022_64
D:\PadMirrorTools\aqt
D:\PadMirrorTools\downloads
%LOCALAPPDATA%\Programs\gstreamer\1.0\msvc_x86_64
```

Có thể override qua parameters/environment:

- `-QtRoot` / `QTDIR`
- `-GStreamerRoot` / `GSTREAMER_ROOT_X86_64`
- `-PortableMsvcRoot` / `PADMIRROR_MSVC_ROOT`
- `-ToolsRoot`
- `-GStreamerVersion`

## Build thủ công với dependency có sẵn

```powershell
$env:PKG_CONFIG_PATH = "$env:GSTREAMER_ROOT_X86_64\lib\pkgconfig;$env:GSTREAMER_ROOT_X86_64\share\pkgconfig"
$env:PATH = "$env:GSTREAMER_ROOT_X86_64\bin;$env:PATH"

cmake -S . -B build\windows-x64 `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:QTDIR" `
  -DPADMIRROR_BUILD_APP=ON `
  -DPADMIRROR_OPENSSL_ROOT="$env:GSTREAMER_ROOT_X86_64" `
  -DPADMIRROR_BUILD_TESTS=ON

cmake --build build\windows-x64 --parallel
ctest --test-dir build\windows-x64 --output-on-failure
```

Manual build không tự tạo stage/ZIP/installer. Dùng build script để kiểm tra đúng release package.

## Tests portable

Không cần Qt/GStreamer/UxPlay runtime:

```bash
cmake -S . -B build/core \
  -DPADMIRROR_BUILD_APP=OFF \
  -DPADMIRROR_BUILD_TESTS=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Hiện tests bao phủ normalize/sanitize receiver config và deterministic receiver identity/MAC. Đây không phải test protocol hoặc iPad compatibility.

## GStreamer factories bắt buộc

Validator yêu cầu:

```text
appsrc
queue
h264parse
h265parse
d3d11h264dec
d3d11h265dec
d3d11convert
videoscale
d3d11videosink
audioconvert
audioresample
volume
level
avdec_alac
wasapi2sink
```

Và file `libfdk-aac-2.dll` bên cạnh executable.

Kiểm tra development runtime:

```powershell
$env:PATH = "$env:GSTREAMER_ROOT_X86_64\bin;$env:PATH"
gst-inspect-1.0 d3d11h264dec
gst-inspect-1.0 d3d11h265dec
gst-inspect-1.0 d3d11convert
gst-inspect-1.0 d3d11videosink
gst-inspect-1.0 wasapi2sink
gst-inspect-1.0 avdec_alac
```

Các property decoder low-latency khác nhau theo GStreamer/driver; code chỉ set property khi factory expose nó.

## Runtime checker

Trên stage:

```powershell
.\build\windows-x64\stage\PadMirrorRuntimeCheck.exe
.\build\windows-x64\stage\PadMirror.exe --check-runtime
```

Kết quả thành công gồm:

```text
OK
TRANSPORT=AIRPLAY_ONLY
BACKEND=UXPLAY_IN_PROCESS
VIDEO=GSTREAMER_D3D11
AUDIO=GSTREAMER_WASAPI2_FDK_AAC_ELD
USB=NOT_INCLUDED
```

`PadMirror.exe --check-runtime-file=<path>` cũng ghi report vào file. Ứng dụng không tải runtime khi component thiếu; cần cài lại từ package đúng hoặc rebuild stage.

## Release stage

GStreamer plugins được copy theo manifest rõ ràng:

- `gstapp.dll`
- `gstcoreelements.dll`
- `gstvideoparsersbad.dll`
- `gstvideoconvertscale.dll`
- `gstaudioconvert.dll`
- `gstaudioresample.dll`
- `gstvolume.dll`
- `gstlevel.dll`
- `gstd3d11.dll`
- `gstlibav.dll`
- `gstwasapi2.dll`

Build script copy toàn bộ `share\licenses` từ đúng GStreamer installation đang dùng vào `licenses\GStreamer`. Đây là inventory authoritative cho GStreamer runtime DLL/plugin và các thư viện transitive được stage; không thay bằng một license blanket.

Source archive được tải, kiểm tra SHA-256 và đóng gói self-contained:

```text
licenses\sources\fdk-aac-2.0.3.tar.gz
  SHA-256 e25671cd96b10bad896aa42ab91a695a9e573395262baed4e4a2ff178d6a3a78

licenses\sources\Qt\qtbase-everywhere-src-6.8.3.tar.xz
  SHA-256 56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80

licenses\sources\Qt\qtsvg-everywhere-src-6.8.3.tar.xz
  SHA-256 35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438
```

FDK source đáp ứng yêu cầu cung cấp source miễn phí của custom license. Qt Base/SVG archives khớp các module runtime do `windeployqt` deploy và chứa license metadata/text theo file của Qt; xem `LICENSES/FDK-AAC-SOURCE.md` và `LICENSES/QT-SOURCE.md`.

Build script fail nếu stage chứa external `uxplay.exe`, driver/INF/SYS, libusb/UsbDk, Python USB bridge, OBS artifacts, QML/QtQml hoặc thư mục USB/QuickTime legacy.

`Assert-NoForbiddenPayload` là release guard, không phải cách chứng minh package đầy đủ; runtime checker thực hiện bước kiểm tra dependency dương tính.

## Installer và firewall

Installer:

- Cài per-user dưới `%LOCALAPPDATA%\Programs\PadMirror`.
- Cài VC++ x64 Redistributable nếu registry báo thiếu.
- Không cài driver, service, filter hoặc external receiver.
- Có task **Allow PadMirror through Windows Firewall** bỏ chọn mặc định.
- Khi task được chọn, installer yêu cầu UAC rồi chạy helper program-scoped cho TCP/UDP trên Private/Domain.

Bản portable không tự sửa firewall. Người dùng có thể chủ động chạy elevated:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\dependencies\configure-airplay-firewall.ps1 `
  -ExePath .\PadMirror.exe
```

Không chạy helper trong build/test automation vì nó thay đổi hệ thống.

## Licensing khi phát hành

`PadMirror.exe` kết hợp code UxPlay-derived GPLv3 nên bản phân phối phải tuân GPL-3.0-or-later. Trước khi phát hành binary công khai, cung cấp Corresponding Source của đúng version build (source, vendored modifications, CMake và build scripts) theo một phương thức GPLv3 chấp nhận được, đồng thời giữ notices/licenses của dependency.

Release stage kèm GPL text, third-party notice, UxPlay/Playfair/llhttp/libplist license texts, FDK AAC custom license và exact source, Qt LGPL text/source inventory, và GStreamer package license inventory. Không được hiểu `QT-LGPL-3.0-only.txt` là blanket license cho mọi Qt file; authoritative terms nằm trong source/module inventory của đúng Qt build. Xem `LICENSES/THIRD_PARTY.md`, `LICENSES/FDK-AAC-SOURCE.md` và `LICENSES/QT-SOURCE.md`.
