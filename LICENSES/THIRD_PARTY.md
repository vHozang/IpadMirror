# Third-party notices

PadMirror includes or redistributes the components below. This document records
the release composition currently produced by `scripts/build-windows.ps1`; it
does not replace the complete license texts in this repository or in a staged
GStreamer/Qt distribution.

PadMirror's own source is licensed under **GPL-3.0-or-later**. See `LICENSE`.
This choice is required by the GPLv3-or-later UxPlay renderer and Playfair code
linked into `PadMirror.exe`; it does not relicense third-party components that
retain their own terms.

The release does **not** contain an external `uxplay.exe`, USB/QuickTime capture,
Apple driver/filter code, libusb/UsbDk, a Python USB bridge, OBS integration,
QML/Qt Quick, or the retired loopback API.

## Components linked into `PadMirror.exe`

| Component | Use and source baseline | License evidence |
| --- | --- | --- |
| UxPlay-derived native receiver and GStreamer renderers | RAOP/RTSP, pairing, mirroring, mDNS and direct audio/video rendering. Vendored from [FDH2/UxPlay](https://github.com/FDH2/UxPlay) commit `fd5192b06f0c2ab2f664b8ab778ac6f772e4d741`, with PadMirror Windows integration and latency/reliability modifications. | UxPlay is GPLv3; renderer source grants GPL version 3 or later. Full GPLv3 text: `third_party/uxplay-native/LICENSE`. Much of `lib/` is separately LGPL-2.1-or-later; see the headers of the individual files. |
| Playfair | FairPlay client authentication, vendored at `third_party/uxplay-native/lib/playfair/`. | GPLv3. Full text: `third_party/uxplay-native/lib/playfair/LICENSE.md`. |
| llhttp | RTSP/HTTP parser, vendored at `third_party/uxplay-native/lib/llhttp/`. | MIT. Full text: `third_party/uxplay-native/lib/llhttp/LICENSE-MIT`. |
| Bundled mDNS responder | Publishes `_raop._tcp` and `_airplay._tcp`; files under `third_party/uxplay-native/lib/mdnsd/`. No external Bonjour service or SDK is shipped. | LGPL-2.1-or-later, as stated in the source headers. |
| libplist C core | Apple property-list parsing/generation, compiled statically from `third_party/libplist/`. | LGPL-2.1-or-later. Full text: `third_party/libplist/COPYING.LESSER.txt`. Embedded `jsmn` and `time64` sources carry MIT notices in their source headers. |
| OpenSSL 3 | TLS and pairing cryptography, linked dynamically from the GStreamer MSVC runtime. | Apache-2.0. The release copies the OpenSSL license from the installed GStreamer license inventory. Upstream: [openssl.org](https://www.openssl.org/). |

The vendored native source intentionally keeps upstream copyright, modification
and per-file license notices. `third_party/README.md` records the exact baseline
and the local modification scope.

## Libraries and runtime files redistributed beside the executable

| Component | Use | License/redistribution note |
| --- | --- | --- |
| Qt 6.8.3 (`Qt6Core`, `Qt6Gui`, `Qt6Widgets` and deployment dependencies, including `Qt6Network` and `Qt6Svg`) | Desktop GUI and Windows integration. Qt is dynamically linked; PadMirror does not ship Qt Quick/QML. | Qt modules have module/file-specific open-source terms, commonly LGPL-3.0-only or GPL alternatives, or a commercial license. Release packaging stages the official Qt Base and Qt SVG source archives, which carry the license metadata/texts for the deployed files, plus the LGPLv3 text. It preserves replacement/relinking rights required by LGPL. See `LICENSES/QT-SOURCE.md` and [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations). |
| GStreamer 1.28.5 MSVC runtime and selected plugins | `appsrc`, parsers, D3D11 decode/render, audio conversion/resampling, level/volume, ALAC decode and WASAPI2 output. | GStreamer core and many plugins are LGPL; individual plugins and bundled libraries can use other compatible licenses. The build stages the license inventory from the exact GStreamer installation, not a single blanket license. See [GStreamer licensing](https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html). |
| FFmpeg/libav through `gstlibav.dll` | ALAC decoding through `avdec_alac`. | The selected GStreamer MSVC package identifies its FFmpeg build as LGPL-2.1-or-later; the matching package license inventory is staged. Different FFmpeg builds can enable GPL/nonfree parts and must be reviewed separately. |
| Fraunhofer FDK AAC 2.0.3 (`libfdk-aac-2.dll`) | AAC-ELD AOT-39 decoding, loaded dynamically. | Custom Fraunhofer FDK AAC license. It requires the complete license text with binary redistribution and free availability of the corresponding FDK AAC source; it expressly grants no patent license. Full text: `LICENSES/FDK-AAC-LICENSE.txt`. Source: [mstorsjo/fdk-aac v2.0.3](https://github.com/mstorsjo/fdk-aac/tree/v2.0.3). |
| Microsoft Visual C++ runtime | C/C++ runtime DLLs and Microsoft redistributable installer. | Microsoft Visual C++ Redistributable license terms apply. |

The staged GStreamer directory contains more runtime DLLs than the selected
plugin manifest because those DLLs satisfy transitive runtime dependencies.
Accordingly, the authoritative binary-release notice set is the complete
license inventory copied from the exact GStreamer installation used for the
build.

## Binary release obligations

Before distributing an installer or portable ZIP:

1. Provide the complete Corresponding Source for the exact PadMirror binary by a
   GPLv3-compliant method. This includes PadMirror source, the vendored UxPlay and
   libplist source, local modifications, CMake files, and build/packaging scripts.
2. Keep `LICENSE`, this notice, vendored license files, FDK AAC's complete custom
   license, the staged Qt Base/SVG source and LGPL text, and the complete
   GStreamer package license inventory with the binary.
3. Make the exact FDK AAC 2.0.3 source available free of charge to binary
   recipients, and do not imply that its copyright license grants an AAC patent
   license.
4. Preserve dynamic Qt replacement/relinking rights and do not impose terms that
   prohibit reverse engineering for debugging modifications to LGPL components.
5. Re-run the license inventory if the Qt, GStreamer, OpenSSL, FFmpeg, FDK AAC,
   plugin manifest, or build provenance changes.

This repository does not claim trademark affiliation with Apple, UxPlay, Qt,
GStreamer, Fraunhofer, Microsoft, or their contributors.
