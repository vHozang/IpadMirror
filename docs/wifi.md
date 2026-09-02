# AirPlay trên Wi-Fi/LAN

PadMirror là receiver AirPlay-only. iPad kết nối trực tiếp tới receiver UxPlay native chạy trong `PadMirror.exe`; không có `uxplay.exe`, localhost RTP relay hoặc Bonjour service bên ngoài.

## Điều kiện mạng

- iPad và PC ở cùng LAN/VLAN.
- Không dùng guest Wi-Fi có isolation.
- Tắt `AP isolation`, `client isolation` hoặc policy chặn peer-to-peer LAN.
- Windows network profile nên là `Private`.
- VPN, Hyper-V/WSL virtual adapter hoặc nhiều NIC có thể làm mDNS chọn sai interface; tạm tắt adapter không dùng khi chẩn đoán.
- Multicast DNS UDP 5353 và AirPlay listener traffic tới `PadMirror.exe` không bị firewall chặn.

PadMirror publish `_raop._tcp` và `_airplay._tcp` bằng mDNS responder được bundle trong backend. Listener được bind trước khi service được quảng bá, tránh trường hợp iPad nhìn thấy receiver nhưng không thể kết nối.

## Kết nối

1. Mở PadMirror và chờ trạng thái **READY TO MIRROR**.
2. Trên iPad, mở **Control Center → Screen Mirroring**.
3. Chọn receiver theo tên trong Settings, mặc định là `PadMirror`.
4. Khi kết nối, UI chuyển `Connecting` rồi `Streaming`.

AirPlay bắt buộc thao tác từ iPad. PadMirror không thể tự chọn iPad hoặc tự bắt đầu Screen Mirroring.

## Firewall

Receiver phải nhận inbound TCP/UDP. Release cung cấp helper:

```text
dependencies\configure-airplay-firewall.ps1
```

Helper tạo hai inbound rules:

- `PadMirror AirPlay (UDP In)`
- `PadMirror AirPlay (TCP In)`

Cả hai:

- Gắn với đường dẫn tuyệt đối của `PadMirror.exe`.
- Chỉ áp dụng `Private,Domain`.
- Không mở cho mọi chương trình.
- Có thể chạy lại để thay rules cũ khi executable đổi đường dẫn.

### Installer

Task **Allow PadMirror through Windows Firewall (Private and Domain networks)** bỏ chọn mặc định. Chỉ khi người dùng chọn task, installer mới gọi PowerShell với `runas`; Windows hiển thị UAC trước khi thay đổi firewall.

### Portable

Package portable không tự thay firewall. Từ PowerShell **Run as Administrator** tại thư mục package:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\dependencies\configure-airplay-firewall.ps1 `
  -ExePath .\PadMirror.exe
```

Không nên tạo rule cho `uxplay.exe`: file đó không tồn tại trong kiến trúc PadMirror.

## Chẩn đoán discovery

Nếu iPad không thấy receiver:

1. Xác nhận PadMirror đang ở **Ready**, không phải Error/Stopped.
2. Mở **Diagnostics** và kiểm tra RAOP listener có TCP port.
3. Xác nhận iPad/PC cùng subnet và không ở guest network.
4. Kiểm tra Windows profile là Private:

   ```powershell
   Get-NetConnectionProfile
   ```

5. Kiểm tra rules:

   ```powershell
   Get-NetFirewallRule -DisplayName "PadMirror*" |
     Format-Table DisplayName, Enabled, Direction, Action, Profile
   ```

6. Tắt tạm VPN/virtual NIC không dùng, restart receiver rồi mở lại Screen Mirroring.
7. Xem `padmirror.log` và `gstreamer.log` từ nút **Open Log Folder** trong Diagnostics.

Nếu receiver xuất hiện nhưng kết nối thất bại, kiểm tra TCP rule, listener port và log RAOP/RTSP. Nếu kết nối có hình nhưng không âm thanh, kiểm tra runtime report có `libfdk-aac-2.dll`, `wasapi2sink` và thiết bị audio output hợp lệ.

## Receiver identity

PadMirror tạo MAC locally administered, unicast và ổn định từ receiver identity. Identity ổn định giúp iPad không coi mỗi lần start là một receiver hoàn toàn mới. Đổi receiver name/FPS làm receiver restart và publish lại records; volume/mute không cần restart.

## Độ trễ

AirPlay latency phụ thuộc Wi-Fi congestion, AP scheduling, iPadOS, GPU decode, audio device và display. Dùng Ethernet cho PC khi có thể, Wi-Fi 5/6 ổn định cho iPad, tránh repeater/mesh hop và đo thực tế theo [latency-testing.md](latency-testing.md).
