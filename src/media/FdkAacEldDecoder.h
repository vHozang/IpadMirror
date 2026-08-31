#pragma once

// In-process AAC-ELD decoder backed by libfdk-aac, loaded dynamically at
// runtime. AirPlay screen-mirroring audio is AAC-ELD (AOT 39). The GStreamer
// libav decoder (avdec_aac) does NOT truly decode AAC-ELD -- it mis-parses the
// ELD spectral/band layout and produces garbled or silent PCM -- which is why
// upstream UxPlay REQUIRES libfdk-aac for this format. PadMirror decodes the
// AAC-ELD access units here and feeds raw PCM S16 into the GStreamer pipeline,
// bypassing avdec_aac entirely.
//
// The library is loaded with LoadLibrary/GetProcAddress rather than linked:
//   * libfdk-aac ships as a MinGW/UCRT build in the toolchain; dynamic loading
//     avoids an MSVC<->MinGW import-library/ABI mismatch (the exported symbols
//     are undecorated C names and x64 has a single calling convention, so the
//     resolved function pointers are called safely from MSVC-built code).
//   * it keeps the Fraunhofer-licensed code out of the static link graph.

#include <cstdint>
#include <string>
#include <vector>

namespace padmirror::media {

// Decodes AAC-ELD access units to interleaved 16-bit PCM. Not thread-safe; a
// single AudioPipeline owns one instance and calls decode() from one thread.
class FdkAacEldDecoder {
public:
    FdkAacEldDecoder() = default;
    ~FdkAacEldDecoder();

    FdkAacEldDecoder(const FdkAacEldDecoder&) = delete;
    FdkAacEldDecoder& operator=(const FdkAacEldDecoder&) = delete;

    // Loads libfdk-aac, opens a raw (TT_MP4_RAW) decoder and configures it with
    // the AudioSpecificConfig (ASC) bytes. Returns false (and sets error()) if
    // the library is unavailable or configuration fails. asc is the same
    // codec_data the pipeline would otherwise hand to a GStreamer AAC decoder.
    bool open(const std::uint8_t* asc, std::size_t ascLen);

    // Decodes one AAC-ELD access unit. On success appends interleaved S16
    // samples to pcmOut and returns true; sampleRate/channels report the PCM
    // format the decoder produced (44100 Hz / 2 ch for AirPlay screen audio).
    // A frame the decoder rejects returns false without touching pcmOut.
    bool decode(
        const std::uint8_t* data,
        std::size_t len,
        std::vector<std::int16_t>& pcmOut,
        int& sampleRate,
        int& channels);

    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] const std::string& error() const { return error_; }

    // Format the decoder reports after configuring with the ASC. These reflect
    // the ACTUAL decoded PCM (output sample rate, channel count, samples per
    // channel per frame), so the pipeline can build matching caps and derive a
    // sample-accurate buffer duration instead of hardcoding assumptions. 0 if
    // the decoder did not report a value yet.
    [[nodiscard]] int sampleRate() const { return sampleRate_; }
    [[nodiscard]] int channels() const { return channels_; }
    [[nodiscard]] int frameSize() const { return frameSize_; }

private:
    void close();

    void* library_ = nullptr;   // HMODULE
    void* handle_ = nullptr;    // HANDLE_AACDECODER
    std::string error_;

    // Resolved libfdk-aac entry points (see FdkAacEldDecoder.cpp for the ABI).
    void* fnOpen_ = nullptr;
    void* fnConfigRaw_ = nullptr;
    void* fnFill_ = nullptr;
    void* fnDecodeFrame_ = nullptr;
    void* fnClose_ = nullptr;
    void* fnGetStreamInfo_ = nullptr;

    int sampleRate_ = 0;
    int channels_ = 0;
    int frameSize_ = 0;
};

} // namespace padmirror::media
