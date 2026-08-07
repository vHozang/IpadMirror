# Third-party boundaries

PadMirror không chép source của các project tham chiếu vào `src/`.

- `ios-screen-record`: tham chiếu chính cho QuickTime-style USB protocol; MIT.
- `quicktime_video_hack`: tham chiếu giao thức phụ; không phải runtime backend.
- `libusb`: direct USB access.
- `libimobiledevice`/`libusbmuxd`: metadata, trust và device communication tùy chọn.
- GStreamer: media pipeline và platform plugins.
- UxPlay: AirPlay receiver GPLv3 chạy ở tiến trình riêng; release kèm binary, license, source archive và patch chọn card LAN vật lý.

Xem [LICENSES/THIRD_PARTY.md](../LICENSES/THIRD_PARTY.md) trước khi phân phối binary.
