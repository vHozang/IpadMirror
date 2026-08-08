# Third-party notices

Tài liệu này ghi nhận dependency và nguồn tham chiếu; không thay thế việc review license trước khi phân phối.

| Thành phần | Vai trò | License/ghi chú |
| --- | --- | --- |
| ios-screen-record | Tham chiếu protocol và fixture kiểm chứng cục bộ | MIT |
| quicktime_video_hack | Tham chiếu protocol phụ | Xem license upstream tại version sử dụng |
| libusb | USB transport cho backend non-Windows/legacy | LGPL-2.1-or-later theo upstream package |
| libimobiledevice | Device metadata/trust tùy chọn | LGPL-2.1 |
| libusbmuxd | usbmux client dependency | LGPL-2.1 |
| pymobiledevice3 10.5.0 | Apple RemoteXPC/DisplayService bridge ngoài tiến trình | GPLv3; source tarball và bridge source được đóng gói dưới `usb-bridge/source` |
| PyInstaller | Đóng gói Python bridge | GPL với bootloader exception; xem license trong bundle dependency |
| GStreamer core/plugins | Decode, render và audio | License thay đổi theo từng plugin; kiểm tra bộ plugin được đóng gói |
| Qt 6 | GUI/QML | LGPL/commercial tùy cách phân phối |
| UxPlay 1.74 | AirPlay receiver ngoài tiến trình | GPLv3; bản phát hành kèm license, README và corresponding-source archive dưới `uxplay/licenses/UxPlay` |

Nguồn chính thức:

- https://github.com/YueChen-C/ios-screen-record
- https://github.com/danielpaulus/quicktime_video_hack
- https://github.com/libusb/libusb
- https://github.com/libimobiledevice/libimobiledevice
- https://github.com/libimobiledevice/libusbmuxd
- https://github.com/doronz88/pymobiledevice3
- https://pyinstaller.org/
- https://gstreamer.freedesktop.org/
- https://www.qt.io/licensing/
- https://github.com/FDH2/UxPlay
