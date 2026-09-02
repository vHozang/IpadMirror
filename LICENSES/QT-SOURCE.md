# Qt corresponding source

PadMirror release builds currently redistribute dynamically linked Qt 6.8.3
runtime files produced by `windeployqt` from the official MSVC 2022 x64 Qt
package. The deployed modules come from Qt Base and Qt SVG.

Exact upstream source archives:

| Module | Archive | SHA-256 |
| --- | --- | --- |
| Qt Base 6.8.3 | <https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz> | `56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80` |
| Qt SVG 6.8.3 | <https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtsvg-everywhere-src-6.8.3.tar.xz> | `35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438` |

The release build verifies and stages both archives under `licenses/sources/Qt/`.
Those source archives include Qt's module/file-specific license metadata and
license texts. `QT-LGPL-3.0-only.txt` is also staged beside this notice for
convenient access to the LGPL version 3 text; it is not a claim that every Qt
file uses only that license.

PadMirror links Qt dynamically. Recipients may replace the deployed Qt shared
libraries with compatible modified builds, subject to the normal ABI and
platform constraints. The distribution must not prohibit reverse engineering
needed to debug such modifications.

If the Qt version, modules, deployment output, build provenance, or local Qt
modifications change, update this inventory, archive hashes, and staged sources
before shipping. Local Qt modifications must be supplied as part of the
corresponding Qt source.
