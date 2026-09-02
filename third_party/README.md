# Vendored source

PadMirror builds the following third-party source directly into
`PadMirror.exe`. Keep upstream copyright and license headers intact when
updating or modifying these directories.

## `uxplay-native`

- Upstream: <https://github.com/FDH2/UxPlay>
- Baseline commit: `fd5192b06f0c2ab2f664b8ab778ac6f772e4d741`
- Product integration: native in-process AirPlay/RAOP receiver; no external
  `uxplay.exe`, localhost RTP relay, or external Bonjour dependency.
- Local modifications include Windows/MSVC portability, bundled mDNS behavior,
  receiver lifecycle/callback safety, D3D11 video rendering, AAC-ELD/ALAC audio,
  timestamp/flush handling, low-latency queue policy, stable identity and
  PadMirror callback/diagnostics integration.
- License: the combined UxPlay-derived target is GPLv3-compatible and includes
  GPL-3.0-or-later renderer code. Individual `lib/` files also retain
  LGPL-2.1-or-later, GPLv3, or MIT terms as stated in their headers and license
  files.
- License files:
  - `uxplay-native/LICENSE` — GNU GPL version 3 text.
  - `uxplay-native/lib/playfair/LICENSE.md` — Playfair GNU GPL version 3 text.
  - `uxplay-native/lib/llhttp/LICENSE-MIT` — llhttp MIT license.

Do not replace these sources with a separately launched UxPlay executable. When
refreshing the baseline, re-audit protocol callbacks, mDNS publication ordering,
GStreamer element/property availability, renderer reset behavior, and all local
modifications before accepting the update.

## `libplist`

- Upstream: <https://github.com/libimobiledevice/libplist>
- Scope: the dependency-free C core used by UxPlay RTSP handlers; the C++
  wrappers and command-line tools are not built.
- Build: compiled statically with MSVC as `padmirror_libplist`.
- License: LGPL-2.1-or-later; see `libplist/COPYING.LESSER.txt` and source
  headers. Embedded `jsmn` and `time64` files retain their MIT notices.

## Distribution

The product license and release obligations are documented in `../LICENSE` and
`../LICENSES/THIRD_PARTY.md`. Binary distributions must provide the
Corresponding Source for the exact vendored baseline and PadMirror changes.
