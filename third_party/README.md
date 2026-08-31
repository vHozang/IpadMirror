# Third-party boundaries

PadMirror không chép source của các project tham chiếu vào `src/`.

- `ios-screen-record`: tham chiếu chính cho QuickTime-style USB protocol (chỉ parser/protocol); MIT.
- `quicktime_video_hack`: tham chiếu giao thức phụ; không phải runtime backend.
- `libimobiledevice`/`libusbmuxd`: metadata, trust và device discovery tùy chọn (chỉ dùng ở M2 nếu Apple runtime cho phép; không dùng để capture thô, không rebind/filter driver).
- GStreamer: media pipeline (D3D11 decode/render, WASAPI audio).
- Qt 6: GUI/QML.

## AirPlay native (gated M6/M7)

Receiver AirPlay chạy **trong** `PadMirror.exe`. Khi triển khai, mã có nguồn gốc UxPlay GPLv3 sẽ được vendor dưới `third_party/uxplay-native` với provenance/license được thiết lập trước, và chỉ port phần protocol/control/pairing/crypto/session cần cho screen mirroring. Không chạy `uxplay.exe` ngoài tiến trình, không relay RTP qua loopback. Hiện tại phần này **chưa** được bao gồm.

Xem [LICENSES/THIRD_PARTY.md](../LICENSES/THIRD_PARTY.md) trước khi phân phối binary.
