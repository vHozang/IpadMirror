# Network Interface Selection & VPN Filtering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow users to choose their active network adapter for AirPlay screen mirroring and filter out virtual/VPN adapters (like Radmin VPN, Tailscale, WSL) by default so mDNS advertises the correct physical LAN/Wi-Fi IP.

**Architecture:** Extend `ReceiverConfig` and `ApplicationSettings` with `bindAddress`. Update native `mdnsd.c` to filter virtual/VPN adapters in auto-detection and support explicit IPv4 bind address override via `dnssd`. Expose a network adapter selection dropdown in `SettingsDialog` using `QNetworkInterface`.

**Tech Stack:** C++20, C11, Qt 6.8 Widgets, Windows IP Helper API (`iphlpapi`), GStreamer, CTest / CMake.

**Spec:** `docs/specs/2026-11-26-network-interface-selection.md`

## Global Constraints

- Portable tests (`padmirror_support`) must not depend on Qt or GStreamer (`-DPADMIRROR_BUILD_APP=OFF`).
- Inbound firewall rules and permissions must remain untouched.
- Clean receiver restart must trigger automatically if `bindAddress` is changed.

## Review Focus

1. **Virtual/VPN metric priority inversion:** Radmin VPN or Tailscale with metric = 1 must be skipped during "Auto" mode in favor of physical Wi-Fi/Ethernet.
2. **Invalid/stale IP in config:** If user configured a static IP that is no longer assigned to any adapter, system must gracefully fall back to Auto rather than failing mDNS start.
3. **Empty / "auto" bindAddress normalization:** Whitespace or invalid string representations must normalize to empty string (Auto mode).
4. **Adapter disconnect/reconnect:** Switching networks must not crash mDNS listener thread.
5. **No regression in portable tests:** CTest on `padmirror_support` must pass without Qt libraries.

---

### Task 1: ReceiverConfig Model, Normalization & Portable Tests

**Files:**
- Modify: `src/support/ReceiverConfig.h:27-44`
- Modify: `src/support/ReceiverConfig.cpp:61-84`
- Test: `tests/support/ReceiverConfigTest.cpp`

**Interfaces:**
- Produces: `std::string ReceiverConfig::bindAddress` (default `""`).
- Produces: `bool receiverRestartRequired(const ReceiverConfig& before, const ReceiverConfig& after)` updated to compare `bindAddress`.

- [ ] **Step 1: Write failing test in `tests/support/ReceiverConfigTest.cpp`**

```cpp
TEST(ReceiverConfigTest, NormalizesBindAddressAndDetectsRestart) {
    ReceiverConfig config;
    config.bindAddress = "  192.168.0.224  ";
    const auto normalized = normalizeReceiverConfig(config);
    EXPECT_EQ(normalized.bindAddress, "192.168.0.224");

    ReceiverConfig changed = normalized;
    changed.bindAddress = "192.168.0.100";
    EXPECT_TRUE(receiverRestartRequired(normalized, changed));

    ReceiverConfig invalid = config;
    invalid.bindAddress = "invalid-ip-address";
    EXPECT_EQ(normalizeReceiverConfig(invalid).bindAddress, "");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/core --output-on-failure -R receiver_config_test`
Expected: Compilation failure or FAIL (missing `bindAddress` member).

- [ ] **Step 3: Implement `bindAddress` in `src/support/ReceiverConfig.h` and `src/support/ReceiverConfig.cpp`**

Add `std::string bindAddress;` to `struct ReceiverConfig`.
Implement IPv4 address trimming and validation in `normalizeReceiverConfig`.
Include `before.bindAddress != after.bindAddress` in `receiverRestartRequired`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/core && ctest --test-dir build/core --output-on-failure -R receiver_config_test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/support/ReceiverConfig.h src/support/ReceiverConfig.cpp tests/support/ReceiverConfigTest.cpp
git commit -m "feat(config): add bindAddress to ReceiverConfig with validation and restart check"
```

---

### Task 2: Smart Virtual/VPN Adapter Filtering & Custom IPv4 Binding in `mdnsd`

**Files:**
- Modify: `third_party/uxplay-native/lib/mdnsd/mdnsd.h:35-52`
- Modify: `third_party/uxplay-native/lib/mdnsd/mdnsd.c:352-440, 1070-1110`
- Modify: `third_party/uxplay-native/lib/dnssd.h:80-90`
- Modify: `third_party/uxplay-native/lib/dnssd.c:130-150`
- Modify: `src/airplay/UxPlayReceiver.cpp:665-685`

**Interfaces:**
- Consumes: `ReceiverConfig::bindAddress` from Task 1.
- Produces: `int mdnsd_set_bind_ipv4(mdnsd_t *mdnsd, uint32_t ipv4_addr)` in `mdnsd.h`.
- Produces: `void dnssd_set_bind_address(dnssd_t *dnssd, const char *ip_address)` in `dnssd.h`.

- [ ] **Step 1: Implement filtering of virtual/VPN adapters in `mdns_get_default_ipv4` (`mdnsd.c`)**

In `third_party/uxplay-native/lib/mdnsd/mdnsd.c`:
Check `adapter->Description` and `adapter->FriendlyName`:
Ignore adapters containing: `"Radmin"`, `"Tailscale"`, `"TAP"`, `"WSL"`, `"Hyper-V"`, `"Virtual"`, `"Wintun"`, `"ZeroTier"`.
Prioritize `IF_TYPE_IEEE80211` (Wi-Fi) and physical `IF_TYPE_ETHERNET_CSMACD`.

- [ ] **Step 2: Add explicit bind address override function in `mdnsd` and `dnssd`**

Implement `mdnsd_set_bind_ipv4(mdnsd_t *mdnsd, uint32_t ipv4_addr)`:
Store `custom_ipv4_addr` on `mdnsd_t`. In `mdnsd_start`, if `custom_ipv4_addr != 0`, use it directly instead of `mdns_get_default_ipv4()`.
Expose `dnssd_set_bind_address(dnssd_t *dnssd, const char *ip_address)` and forward to underlying mdns implementation.

- [ ] **Step 3: Connect `config.bindAddress` in `UxPlayReceiver.cpp`**

In `UxPlayReceiver::start()`:
If `!config.bindAddress.empty()`, call `dnssd_set_bind_address(dnssd_, config.bindAddress.c_str())`.

- [ ] **Step 4: Verify build with CMake**

Run: `cmake --build build/windows-x64 --target padmirror_airplay --parallel`
Expected: Clean compilation with no errors.

- [ ] **Step 5: Commit**

```bash
git add third_party/uxplay-native/ src/airplay/UxPlayReceiver.cpp
git commit -m "feat(airplay): filter virtual VPN adapters and support custom bind IP in mdnsd"
```

---

### Task 3: Application Settings Persistence & Settings Dialog UI

**Files:**
- Modify: `src/app/ApplicationSettings.cpp:8-48`
- Modify: `src/app/SettingsDialog.h:20-40`
- Modify: `src/app/SettingsDialog.cpp:20-110`

**Interfaces:**
- Consumes: `ReceiverConfig::bindAddress` from Task 1.
- Produces: Persistent `network/bindAddress` in QSettings.
- Produces: QComboBox `networkInterface_` in `SettingsDialog`.

- [ ] **Step 1: Persist `bindAddress` in `ApplicationSettings.cpp`**

In `loadConfig()`: read `settings.value(QStringLiteral("network/bindAddress"), QString()).toString().toStdString()`.
In `saveConfig()`: write `settings.setValue(QStringLiteral("network/bindAddress"), QString::fromStdString(config.bindAddress))`.

- [ ] **Step 2: Populate Network Interface combo box in `SettingsDialog.cpp`**

Include `<QNetworkInterface>`.
Add `QComboBox* networkInterface_` to form.
Add item `"Auto (Khuyên dùng / Recommended)"` with userData `""`.
Enumerate active non-loopback IPv4 interfaces from `QNetworkInterface::allInterfaces()`.
For each interface with a valid IPv4 address:
Add item `"<Name> (<IPv4>)"` with userData `"<IPv4>"`.
Set current index matching `config.bindAddress` (or default to 0).
In `SettingsDialog::accept()`:
`config.bindAddress = networkInterface_->currentData().toString().toUtf8().toStdString();`

- [ ] **Step 3: Build the full application**

Run: `cmake --build build/windows-x64 --target padmirror_app --parallel`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/app/ApplicationSettings.cpp src/app/SettingsDialog.h src/app/SettingsDialog.cpp
git commit -m "feat(ui): add network adapter selection to SettingsDialog"
```

---

### Task 4: Full Build, CTest & System Verification

**Files:**
- Verification only

- [ ] **Step 1: Run all unit tests**

Run: `ctest --test-dir build/windows-x64 --output-on-failure`
Expected: 100% tests pass.

- [ ] **Step 2: Run Runtime Check validator**

Run: `.\build\windows-x64\PadMirrorRuntimeCheck.exe`
Expected: All GStreamer & runtime dependencies reported healthy.

- [ ] **Step 3: End-to-End Live Verification**

Launch `PadMirror.exe`, open Settings dialog:
1. Verify dropdown shows "Auto" as well as Wi-Fi (`192.168.0.224`), Ethernet, Radmin VPN.
2. Confirm mDNS now advertises `192.168.0.224` even when Radmin VPN is connected.
3. Test iPad Screen Mirroring discovery.

- [ ] **Step 4: Final commit & diff check**

Run: `git diff --check`
Commit any remaining cleanup.
