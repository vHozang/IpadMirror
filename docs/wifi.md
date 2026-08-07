# Mirror khi cùng Wi-Fi

Wi-Fi dùng AirPlay qua UxPlay và là đường dự phòng. USB vẫn là lựa chọn cho PUBG vì ít jitter và ít tích lũy latency hơn.

## Yêu cầu mạng

- iPad và máy tính ở cùng LAN/VLAN.
- Tắt `AP isolation`, `client isolation` hoặc guest-network isolation.
- Windows network profile nên là `Private`; PadMirror vẫn tạo rule cho mọi profile để hoạt động khi LAN đang bị đặt là `Public`.
- Cho phép `uxplay.exe` qua firewall trên TCP/UDP `7100-7102`.

PadMirror nhận RTP nội bộ trên loopback:

- Video H.264: `127.0.0.1:50100`.
- Audio L16: `127.0.0.1:50102`.

Hai cổng này không cần mở ra LAN.

## UxPlay trên Windows

Bản installer và portable đã kèm UxPlay 1.74 tại `uxplay\uxplay.exe`, các DLL/plugin MinGW cần thiết, GPLv3 và source archive đúng revision. Không cần cài MSYS2 hoặc GStreamer MinGW trên máy người dùng.

Nếu tự build, UxPlay 1.74+ có mDNS tích hợp và có thể build trong MSYS2 UCRT64:

```bash
pacman -Syu
pacman -S --needed git \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-libplist \
  mingw-w64-ucrt-x86_64-gstreamer \
  mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good \
  mingw-w64-ucrt-x86_64-gst-plugins-bad \
  mingw-w64-ucrt-x86_64-gst-libav

git clone https://github.com/FDH2/UxPlay.git
cmake -S UxPlay -B UxPlay/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNO_MARCH_NATIVE=ON
cmake --build UxPlay/build
cmake --install UxPlay/build --prefix /ucrt64
```

Release PadMirror dùng revision đã ghim kèm patch chọn đúng card LAN vật lý. Sau khi cài các package MSYS2 ở trên, chạy trong shell UCRT64:

```bash
./scripts/build-uxplay-windows.sh
```

Đường dẫn thường dùng:

```text
C:\msys64\ucrt64\bin\uxplay.exe
```

## Sử dụng

1. Mở PadMirror -> Settings.
2. Connection -> `AirPlay Wi-Fi`.
3. Bản release tự tìm `uxplay\uxplay.exe`; chỉ nhập đường dẫn khác nếu dùng bản UxPlay riêng.
4. Chọn `Apply and restart receiver`.
5. Trên iPad mở Control Center -> Screen Mirroring -> `PadMirror`.

PadMirror chạy UxPlay với tên receiver `PadMirror`, yêu cầu tối đa 60 FPS, tắt strict A/V sync và forward H.264/L16 vào pipeline GPU/audio của ứng dụng.

## Nếu iPad không thấy PadMirror

1. Mở lại receiver trong PadMirror và chấp nhận cửa sổ UAC cấu hình firewall.
2. Kiểm tra firewall, network profile và tắt guest/client isolation trên router.
3. Đảm bảo hai thiết bị không ở guest Wi-Fi.
4. Kiểm tra cổng `7100-7102` không bị ứng dụng khác dùng.
5. Mở `padmirror.log`; output của UxPlay được ghi với prefix `UxPlay:`. Bản đóng gói ưu tiên UxPlay đi kèm để tránh chọn nhầm card Hyper-V/WSL.

UxPlay là GPLv3 và không được nhúng vào PadMirror. Nếu phân phối UxPlay cùng ứng dụng, phải tuân thủ đầy đủ GPLv3 và cung cấp notice/source tương ứng.
