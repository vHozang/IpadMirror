# Third-party notices

Tài liệu này ghi nhận dependency và nguồn tham chiếu; không thay thế việc review license trước khi phân phối.

Bản phân phối PadMirror V1 hiện tại **không** đóng gói: `uxplay.exe` hay bundle UxPlay ngoài tiến trình, cầu Python (`pymobiledevice3`/PyInstaller), `libusb`/UsbDk, helper cài WinUSB (`libwdi`), hay artifact OBS. Các mục đó đã bị loại khỏi build/package.

| Thành phần | Vai trò | License/ghi chú |
| --- | --- | --- |
| ios-screen-record | Tham chiếu protocol và fixture kiểm chứng cục bộ (chỉ parser/protocol) | MIT |
| quicktime_video_hack | Tham chiếu protocol phụ | Xem license upstream tại version sử dụng |
| libimobiledevice | Device metadata/trust tùy chọn (chỉ khi M2 dùng để discovery/trust, không dùng để capture thô) | LGPL-2.1 |
| libusbmuxd | usbmux client dependency (tùy chọn, như trên) | LGPL-2.1 |
| GStreamer core/plugins | Decode (D3D11), render và audio (WASAPI) | License thay đổi theo từng plugin; kiểm tra bộ plugin được đóng gói |
| Qt 6 | GUI/QML | LGPL/commercial tùy cách phân phối |

## AirPlay (native, in-process) — PRODUCT IS NOW GPL-3.0-or-later

Receiver AirPlay in-process đã được tích hợp: mã nguồn gốc UxPlay (GPL-3.0) được
vendor dưới `third_party/uxplay-native/` và biên dịch trực tiếp vào `PadMirror.exe`
(không có `uxplay.exe` ngoài tiến trình, không RTP relay, không LanReceiver).

**Vì mã GPLv3 được link vào binary, license phân phối của PadMirror là
GPL-3.0-or-later.** Bản phân phối phải kèm corresponding source đầy đủ cùng
copyright/modification notices.

Dependency của AirPlay và license tương ứng:

| Thành phần | Vai trò | License |
| --- | --- | --- |
| UxPlay (protocol/crypto/RAOP/mDNS, vendored `third_party/uxplay-native`) | Receiver AirPlay in-process | GPL-3.0 (một số file lib gốc LGPL-2.1; xem header từng file) |
| libplist (C core, vendored `third_party/libplist`) | Parse/produce Apple property list cho RTSP handlers | LGPL-2.1-or-later |
| llhttp (vendored trong uxplay-native) | HTTP request parser | MIT |
| Playfair (vendored trong uxplay-native) | FairPlay client auth | Xem `third_party/uxplay-native/lib/playfair/LICENSE.md` |
| Bundled mDNSResponder (`lib/mdnsd`) | Service discovery `_airplay._tcp`/`_raop._tcp` | Xem header upstream |
| OpenSSL 3 (crypto/ssl) | Mã hoá pairing/RTSP; link từ bản GStreamer MSVC | Apache-2.0 (OpenSSL 3) |

Không có Apple Bonjour SDK bên ngoài: dùng mDNSResponder tối giản đi kèm UxPlay.

Nguồn chính thức:

- https://github.com/FDH2/UxPlay (GPL-3.0)
- https://github.com/libimobiledevice/libplist (LGPL-2.1)
- https://github.com/nodejs/llhttp (MIT)
- https://www.openssl.org/ (Apache-2.0)
