#include "media/FdkAacEldDecoder.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstring>

namespace padmirror::media {
namespace {

// --- Minimal ABI mirror of the libfdk-aac decoder API ---------------------
// We resolve these by name at runtime rather than linking the import library,
// so we restate exactly the signatures libfdk-aac exports (aacdecoder_lib.h /
// FDK_audio.h / machine_type.h). x64 Windows has a single calling convention,
// so the plain function-pointer types below match the DLL's exports.
//
//   TRANSPORT_TYPE TT_MP4_RAW      = 0   (raw, packet-based access units)
//   AAC_DECODER_ERROR AAC_DEC_OK   = 0
//   INT_PCM                        = 16-bit signed (SAMPLE_BITS 16)
//
// Only the three leading CStreamInfo fields are read (sampleRate, frameSize,
// numChannels); the pointer returned by GetStreamInfo aliases the decoder's
// real (larger) struct, and we never read past these first three INTs.
constexpr int kTtMp4Raw = 0;
constexpr int kAacDecOk = 0;

struct MinimalStreamInfo {
    int sampleRate;
    int frameSize;
    int numChannels;
    // (further fields exist in the real CStreamInfo but are not read here)
};

using AacHandle = void*;
using FnOpen = AacHandle(*)(int transportFmt, unsigned int nrOfLayers);
using FnConfigRaw = int(*)(AacHandle self, unsigned char* conf[], const unsigned int length[]);
using FnFill = int(*)(AacHandle self, unsigned char* pBuffer[], const unsigned int bufferSize[], unsigned int* bytesValid);
using FnDecodeFrame = int(*)(AacHandle self, short* pTimeData, const int timeDataSize, const unsigned int flags);
using FnClose = void(*)(AacHandle self);
using FnGetStreamInfo = MinimalStreamInfo*(*)(AacHandle self);

// AAC-ELD screen-mirror frame = 480 samples/ch; allow generous headroom (any
// AAC frame is <= 2048 samples/ch) x max channels so DecodeFrame never
// overflows the output buffer.
constexpr int kMaxSamplesPerFrame = 2048;
constexpr int kMaxChannels = 8;
constexpr int kDecodeBufferSamples = kMaxSamplesPerFrame * kMaxChannels;

} // namespace

FdkAacEldDecoder::~FdkAacEldDecoder() {
    close();
}

bool FdkAacEldDecoder::open(const std::uint8_t* asc, std::size_t ascLen) {
    close();
#if !defined(_WIN32)
    error_ = "libfdk-aac AAC-ELD decoding is only wired up on Windows";
    return false;
#else
    if (!asc || ascLen == 0) {
        error_ = "empty AudioSpecificConfig";
        return false;
    }

    // libfdk-aac-2.dll ships beside PadMirror.exe (bundled by the build/deploy
    // scripts) and also lives on PATH in the dev toolchain. LoadLibraryW
    // searches the application directory, so the bare name resolves in both.
    auto* lib = ::LoadLibraryW(L"libfdk-aac-2.dll");
    if (!lib) {
        error_ = "libfdk-aac-2.dll not found (AAC-ELD audio unavailable)";
        return false;
    }
    library_ = lib;

    fnOpen_ = reinterpret_cast<void*>(::GetProcAddress(lib, "aacDecoder_Open"));
    fnConfigRaw_ = reinterpret_cast<void*>(::GetProcAddress(lib, "aacDecoder_ConfigRaw"));
    fnFill_ = reinterpret_cast<void*>(::GetProcAddress(lib, "aacDecoder_Fill"));
    fnDecodeFrame_ = reinterpret_cast<void*>(::GetProcAddress(lib, "aacDecoder_DecodeFrame"));
    fnClose_ = reinterpret_cast<void*>(::GetProcAddress(lib, "aacDecoder_Close"));
    fnGetStreamInfo_ = reinterpret_cast<void*>(::GetProcAddress(lib, "aacDecoder_GetStreamInfo"));
    if (!fnOpen_ || !fnConfigRaw_ || !fnFill_ || !fnDecodeFrame_ || !fnClose_ || !fnGetStreamInfo_) {
        error_ = "libfdk-aac-2.dll is missing expected decoder symbols";
        close();
        return false;
    }

    auto openFn = reinterpret_cast<FnOpen>(fnOpen_);
    handle_ = openFn(kTtMp4Raw, 1);
    if (!handle_) {
        error_ = "aacDecoder_Open failed";
        close();
        return false;
    }

    // Configure with the raw ASC. aacDecoder_ConfigRaw takes arrays of buffer
    // pointers/lengths (one entry for a single-layer RAW stream). Copy the ASC
    // into an owned buffer so the pointer stays valid for the call.
    std::vector<unsigned char> ascCopy(asc, asc + ascLen);
    unsigned char* conf[1] = {ascCopy.data()};
    const unsigned int confLen[1] = {static_cast<unsigned int>(ascLen)};
    auto configRawFn = reinterpret_cast<FnConfigRaw>(fnConfigRaw_);
    const int rc = configRawFn(handle_, conf, confLen);
    if (rc != kAacDecOk) {
        error_ = "aacDecoder_ConfigRaw rejected the AAC-ELD ASC (code " +
                 std::to_string(rc) + ")";
        close();
        return false;
    }

    // Read the format the decoder derived from the ASC (output sample rate,
    // channel count, samples/frame). For AAC-ELD screen audio this is
    // 44100 Hz / 2 ch / 480 samples. The pipeline uses these actual values to
    // build matching caps and a sample-accurate buffer duration rather than
    // hardcoding -- a mismatch here is exactly what makes decoded-but-wrong
    // audio sound harsh/screechy.
    auto infoFn = reinterpret_cast<FnGetStreamInfo>(fnGetStreamInfo_);
    if (auto* info = infoFn(handle_)) {
        sampleRate_ = info->sampleRate;
        channels_ = info->numChannels;
        frameSize_ = info->frameSize;
    }
    error_.clear();
    return true;
#endif
}

bool FdkAacEldDecoder::decode(
    const std::uint8_t* data,
    std::size_t len,
    std::vector<std::int16_t>& pcmOut,
    int& sampleRate,
    int& channels) {
#if !defined(_WIN32)
    (void)data; (void)len; (void)pcmOut; (void)sampleRate; (void)channels;
    return false;
#else
    if (!handle_ || !data || len == 0) return false;

    auto fillFn = reinterpret_cast<FnFill>(fnFill_);
    auto decodeFn = reinterpret_cast<FnDecodeFrame>(fnDecodeFrame_);
    auto infoFn = reinterpret_cast<FnGetStreamInfo>(fnGetStreamInfo_);

    // Hand the whole access unit to the decoder. For RAW packet mode the entire
    // AU is one frame, so a single Fill + DecodeFrame consumes it.
    unsigned char* inBuf[1] = {const_cast<unsigned char*>(data)};
    const unsigned int inSize[1] = {static_cast<unsigned int>(len)};
    unsigned int bytesValid = static_cast<unsigned int>(len);
    const int fillRc = fillFn(handle_, inBuf, inSize, &bytesValid);
    if (fillRc != kAacDecOk) return false;

    short pcm[kDecodeBufferSamples];
    const int decRc = decodeFn(handle_, pcm, kDecodeBufferSamples, 0);
    if (decRc != kAacDecOk) return false;

    auto* info = infoFn(handle_);
    if (!info || info->numChannels <= 0 || info->frameSize <= 0) return false;

    sampleRate = info->sampleRate;
    channels = info->numChannels;
    sampleRate_ = info->sampleRate;
    channels_ = info->numChannels;
    frameSize_ = info->frameSize;
    const std::size_t total =
        static_cast<std::size_t>(info->frameSize) * static_cast<std::size_t>(info->numChannels);
    if (total == 0 || total > static_cast<std::size_t>(kDecodeBufferSamples)) return false;
    pcmOut.insert(pcmOut.end(), pcm, pcm + total);
    return true;
#endif
}

void FdkAacEldDecoder::close() {
#if defined(_WIN32)
    if (handle_ && fnClose_) {
        reinterpret_cast<FnClose>(fnClose_)(handle_);
    }
    handle_ = nullptr;
    if (library_) {
        ::FreeLibrary(static_cast<HMODULE>(library_));
        library_ = nullptr;
    }
#endif
    handle_ = nullptr;
    library_ = nullptr;
    fnOpen_ = fnConfigRaw_ = fnFill_ = fnDecodeFrame_ = fnClose_ = fnGetStreamInfo_ = nullptr;
}

} // namespace padmirror::media
