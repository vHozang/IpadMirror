# Specification: Network Interface Selection & VPN Filtering for PadMirror

- **Date:** 2026-11-26
- **Topic:** Network interface selection and virtual/VPN adapter filtering in AirPlay mDNS discovery.
- **Status:** Proposed (Design Approved by User)

## 1. Context & Problem Statement

PadMirror acts as an AirPlay receiver on Windows. Discovery relies on mDNS (`_airplay._tcp`, `_raop._tcp`) to advertise the receiver's presence and IP address to iPads and iPhones on the local network.

Currently, `mdnsd.c` detects the local IPv4 address using `GetAdaptersAddresses` by picking the network adapter with the lowest `Ipv4Metric` that has a gateway.
When virtual network adapters or VPNs (such as Radmin VPN, Tailscale, Hamachi, WSL, Hyper-V, VMware) are installed, they frequently register with a metric of 1 (higher priority than physical Wi-Fi at metric 35+). As a result:
1. `mdnsd` binds its multicast socket (`IP_MULTICAST_IF`) to the virtual/VPN interface (e.g. `26.57.216.39` for Radmin VPN).
2. `mdnsd` advertises the VPN IP address in its DNS A records.
3. The iPad on the physical Wi-Fi network (`192.168.0.x`) cannot discover or reach the PC.

## 2. Goals & Non-Goals

### Goals
1. **Intelligent Auto-Filtering (Backend Default):**
   - In `mdns_get_default_ipv4()`, ignore known virtual/VPN adapters (Radmin VPN, Tailscale, TAP, WSL, Hyper-V, VirtualBox, VMware) and prefer genuine physical Wi-Fi (`IF_TYPE_IEEE80211`) and Ethernet (`IF_TYPE_ETHERNET_CSMACD`) adapters.
   - Out of the box, "Auto" selects the actual LAN/Wi-Fi connection without requiring manual user intervention.
2. **Explicit UI Selection (Settings Dialog):**
   - Add a "Network Interface" dropdown in `SettingsDialog` displaying:
     - `Tự động / Auto (Recommended)`
     - Each active IPv4 interface with its friendly name and IP (e.g., `Wi-Fi (192.168.0.224)`, `Ethernet (192.168.1.22)`).
3. **Configuration Persistence:**
   - Add `bindAddress` to `ReceiverConfig` (defaulting to empty / `"auto"`).
   - Persist to Windows registry via `ApplicationSettings` under `network/bindAddress`.
4. **Targeted mDNS Binding:**
   - Pass the chosen `bindAddress` to `UxPlayReceiver` and down to `mdnsd`.
   - If specified, `mdnsd` uses that exact IPv4 address for socket binding and mDNS A record advertisement.
5. **Restart Detection:**
   - `receiverRestartRequired()` detects changes to `bindAddress` and restarts the receiver cleanly to re-publish mDNS on the selected interface.

### Non-Goals
- Multi-homed simultaneous dual-interface mDNS broadcasting (AirPlay requires a single consistent endpoint per session).
- IPv6 manual interface binding (IPv6 continues to use link-local scope).

## 3. Architecture & Approaches Considered

### Approach 1 (Chosen): Hybrid Smart Filtering + Manual UI Override
- Default "Auto" uses an improved adapter filter in `mdnsd.c` that skips software/virtual interfaces and prioritizes physical Wi-Fi/Ethernet.
- Settings dialog exposes a dropdown with all active network adapters so users can pin a specific network when multi-homed.
- **Trade-off:** Minimal overhead, best UX (works automatically for 99% of users, allows explicit override for complex setups).

### Approach 2 (Rejected): Manual Selection Only
- Forces the user to configure settings before working if multiple adapters exist.
- **Trade-off:** Poor zero-config experience.

### Approach 3 (Rejected): Bind All Interfaces Simultaneously
- Listens and multicasts across every interface simultaneously.
- **Trade-off:** Breaks AirPlay because clients receive competing A-records with unreachable virtual IPs, causing intermittent connection drops.

## 4. Detailed Component Changes

### 4.1. `padmirror_support` (`src/support/ReceiverConfig.h`, `ReceiverConfig.cpp`)
- Add `std::string bindAddress = "";` to `ReceiverConfig`.
- In `normalizeReceiverConfig`, trim and validate IPv4 string format or retain empty/auto.
- In `receiverRestartRequired`, check `before.bindAddress != after.bindAddress`.
- Add test coverage in `tests/support/ReceiverConfigTest.cpp`.

### 4.2. `third_party/uxplay-native` (`lib/mdnsd/mdnsd.c`, `lib/mdnsd/mdnsd.h`, `lib/dnssd.h`, `lib/dnssd.c`)
- Enhance `mdns_get_default_ipv4()`:
  - Skip adapters where description/name contains virtual keywords (`Radmin`, `Tailscale`, `TAP`, `WSL`, `Hyper-V`, `Virtual`, `Wintun`, `ZeroTier`).
  - Prioritize `IF_TYPE_IEEE80211` (Wi-Fi) and physical `IF_TYPE_ETHERNET_CSMACD`.
- Add `mdnsd_set_bind_ipv4(mdnsd_t *mdnsd, uint32_t ipv4_addr)`:
  - Allows overriding `mdns_get_default_ipv4()` with a user-selected IPv4 address.
- Expose helper through `dnssd` API to set custom bind address.

### 4.3. `padmirror_airplay` (`src/airplay/UxPlayReceiver.cpp`, `UxPlayReceiver.h`)
- In `UxPlayReceiver::start`, parse `config.bindAddress` if set.
- Pass the parsed IPv4 address to `dnssd_` / `mdnsd_`.

### 4.4. `padmirror_app_support` (`src/app/ApplicationSettings.cpp`, `SettingsDialog.cpp`, `SettingsDialog.h`)
- In `ApplicationSettings`: serialize/deserialize `network/bindAddress`.
- In `SettingsDialog`:
  - Enumerate interfaces using `QNetworkInterface::allInterfaces()`.
  - Populate `networkInterfaceCombo_` with:
    - `"Auto (Recommended)"` (value: `""`)
    - Up to N valid IPv4 interfaces: `"<Interface Name> (<IPv4>)"` (value: `"<IPv4>"`).
  - Select current configured address or default to Auto.

## 5. Verification & Testing Plan
1. **Portable Unit Tests:**
   - Run `ctest -R receiver_config_test` to verify `bindAddress` normalization and restart requirement.
2. **Settings Dialog Verification:**
   - Launch PadMirror settings, confirm dropdown lists available network adapters (including Wi-Fi `192.168.0.224` and Radmin VPN).
3. **Auto-Selection Verification:**
   - With Radmin VPN active and metric 1, confirm "Auto" selects Wi-Fi (`192.168.0.224`).
4. **Manual Selection Verification:**
   - Explicitly select Wi-Fi, save, verify AirPlay mDNS binds to `192.168.0.224`.
5. **iPad Compatibility Check:**
   - Verify Screen Mirroring on iPad discovers PadMirror immediately on Wi-Fi.
