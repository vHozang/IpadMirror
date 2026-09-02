# FDK AAC corresponding source

PadMirror release builds currently redistribute the unmodified MSYS2 UCRT64
`libfdk-aac-2.dll` package version 2.0.3 for AAC-ELD decoding.

Exact upstream source:

- Project: <https://github.com/mstorsjo/fdk-aac>
- Tag: `v2.0.3`
- Archive: <https://github.com/mstorsjo/fdk-aac/archive/refs/tags/v2.0.3.tar.gz>
- SHA-256: `e25671cd96b10bad896aa42ab91a695a9e573395262baed4e4a2ff178d6a3a78`

Binary recipients must be able to obtain this complete source free of charge.
The source archive is staged in `licenses/sources/` by the release build script,
so the installer and portable ZIP remain self-contained for this requirement.

If the FDK AAC version, build recipe, binary provenance, or local modifications
change, update this file, the archive hash, and the staged source before
shipping. Local modifications must also include prominent modification/date
notices required by the FDK AAC license.

The complete copyright license and no-patent-license notice are in
`FDK-AAC-LICENSE.txt`.
