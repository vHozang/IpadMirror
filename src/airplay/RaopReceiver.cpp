#include "airplay/RaopReceiver.h"

#include "core/IMediaIngress.h"

// UxPlay vendored C headers. RAOP_H pulls in stream.h (video/audio structs),
// dnssd.h, raop_ntp.h. Kept in an extern "C" island.
//
// raop_ntp.h declares a function returning ssize_t, a POSIX type MSVC lacks;
// supply it before including the C API so the wrapper compiles under cl.exe.
#if defined(_MSC_VER)
#include <BaseTsd.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#endif

extern "C" {
#include "raop.h"
#include "dnssd.h"
#include "stream.h"
}

#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

// gethostname() for stableMac(): winsock2.h on Windows (already pulled in
// transitively by the vendored C headers, included explicitly here so this
// TU does not depend on that ordering), unistd.h elsewhere.
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace padmirror::airplay {
namespace {

// UxPlay NTP timestamps are CLOCK_REALTIME nanoseconds (see
// raop_ntp_get_local_time), directly compatible with PadMirror ptsNs.
constexpr std::uint64_t kNoDuration = 0;

// Locally-administered random MAC used as the AirPlay device id, matching
// UxPlay's random_mac(). Used only as a fallback when a stable identity can
// not be derived; PadMirror does not persist pairing state in V1.
std::string randomMac() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 255);
    unsigned char b[6];
    for (auto& x : b) x = static_cast<unsigned char>(dist(rd));
    b[0] = static_cast<unsigned char>((b[0] & ~0x01) | 0x02);
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5]);
    return std::string(buf);
}

// Deterministic locally-administered MAC derived from the machine host name.
// A random MAC per launch makes the iPad register a NEW "PadMirror" receiver
// on every run (the Control Center list fills with duplicates and no receiver
// identity survives a restart). Hashing the stable host name yields the same
// device id across launches on a given PC while staying unique between PCs.
// Falls back to a random MAC if the host name is unavailable.
std::string stableMac() {
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) != 0 || host[0] == '\0') {
        return randomMac();
    }
    // FNV-1a 64-bit over the host name -> 6 MAC bytes.
    std::uint64_t h = 1469598103934665603ULL;
    for (const char* p = host; *p; ++p) {
        h ^= static_cast<unsigned char>(*p);
        h *= 1099511628211ULL;
    }
    unsigned char b[6];
    for (int i = 0; i < 6; ++i) {
        b[i] = static_cast<unsigned char>((h >> (8 * i)) & 0xFF);
    }
    // Locally-administered, unicast (matches UxPlay's random_mac convention).
    b[0] = static_cast<unsigned char>((b[0] & ~0x01) | 0x02);
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5]);
    return std::string(buf);
}

core::VideoCodec toVideoCodec(bool isH265) {
    return isH265 ? core::VideoCodec::Hevc : core::VideoCodec::H264;
}

// Scan an Annex-B byte-stream (4-byte 0x00000001 start codes, as emitted by
// raop_rtp_mirror.c) for a keyframe NAL unit. The D3D11 decoder must be told
// which access units are sync points: without this every frame is flagged
// DELTA_UNIT, so after any flush/discontinuity the decoder decodes P-frames
// against references it never received and renders torn/garbled output. An
// access unit counts as a keyframe if it carries an IDR slice (H264 NAL
// type 5; H265 NAL types 16-21 BLA/IDR/CRA) -- SPS/PPS/VPS that precede it in
// the same AU come along for free since the whole AU shares one PTS.
bool containsKeyframe(const std::uint8_t* data, int len, bool isH265) {
    if (!data || len < 5) return false;
    for (int i = 0; i + 4 < len; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            const std::uint8_t header = data[i + 4];
            if (isH265) {
                const int nalType = (header >> 1) & 0x3F;
                if (nalType >= 16 && nalType <= 21) return true;
            } else {
                const int nalType = header & 0x1F;
                if (nalType == 5) return true;
            }
            i += 4;  // skip past this start code before scanning again
        }
    }
    return false;
}

// AirPlay audio compression type (ct): 2=ALAC, 4=AAC-MAIN, 8=AAC-ELD.
core::AudioCodec toAudioCodec(unsigned char ct) {
    switch (ct) {
    case 8:  return core::AudioCodec::AacEld;
    default: return core::AudioCodec::Unknown;
    }
}

} // namespace

// Callback context: everything a UxPlay callback needs. Held by shared_ptr so
// its lifetime survives until all native worker threads have stopped, even if
// RaopReceiver::stop races with an in-flight callback.
struct RaopReceiverContext {
    core::IMediaIngress* ingress = nullptr;
    media::AirPlayClockSynchronizer clock;
    std::atomic<bool> active{false};
    // Latched formats so every packet carries current codec metadata.
    std::atomic<unsigned char> audioCt{8};
    std::atomic<int> videoIsH265{0};
};

namespace {

// UxPlay invokes callbacks with C linkage and its own typed signatures. These
// trampolines match raop_callbacks_t exactly and forward to the shared context.
extern "C" {

void pmVideoProcess(void* cls, raop_ntp_t* /*ntp*/, video_decode_struct* data) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (!ctx || !ctx->active.load() || !ctx->ingress) return;
    if (!data || !data->data || data->data_len <= 0) return;

    const auto normalized = ctx->clock.normalize(
        media::AirPlayClockSynchronizer::Stream::Video,
        data->ntp_time_remote ? data->ntp_time_remote : data->ntp_time_local,
        kNoDuration,
        data->ntp_time_local);

    core::EncodedVideoPacket packet;
    packet.codec = toVideoCodec(data->is_h265);
    packet.payload = std::make_shared<const std::vector<std::uint8_t>>(
        data->data, data->data + data->data_len);
    packet.ptsNs = normalized.ptsNs;
    packet.keyframe = containsKeyframe(data->data, data->data_len, data->is_h265);
    packet.discontinuity = normalized.rebased;
    packet.format.codec = packet.codec;
    ctx->ingress->pushVideo(std::move(packet));
}

void pmAudioProcess(void* cls, raop_ntp_t* /*ntp*/, audio_decode_struct* data) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (!ctx || !ctx->active.load() || !ctx->ingress) return;
    if (!data || !data->data || data->data_len <= 0) return;

    const auto codec = toAudioCodec(data->ct);
    // PadMirror V1's audio pipeline supports AAC-ELD (and PCM). Drop unsupported
    // compression types rather than feed the decoder garbage.
    if (codec == core::AudioCodec::Unknown) return;

    const auto normalized = ctx->clock.normalize(
        media::AirPlayClockSynchronizer::Stream::Audio,
        data->ntp_time_remote ? data->ntp_time_remote : data->ntp_time_local,
        kNoDuration,
        data->ntp_time_local);

    core::EncodedAudioPacket packet;
    packet.codec = codec;
    packet.payload = std::make_shared<const std::vector<std::uint8_t>>(
        data->data, data->data + data->data_len);
    packet.ptsNs = normalized.ptsNs;
    packet.discontinuity = normalized.rebased;
    packet.format.codec = codec;
    ctx->ingress->pushAudio(std::move(packet));
}

int pmVideoSetCodec(void* cls, video_codec_t codec) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx) {
        ctx->videoIsH265.store(codec == VIDEO_CODEC_H265 ? 1 : 0);
        if (ctx->ingress) {
            ctx->ingress->signalDiscontinuity(
                core::DiscontinuityReason::VideoFormatChanged);
        }
    }
    return 0;
}

void pmAudioGetFormat(
    void* cls, unsigned char* ct, unsigned short* /*spf*/,
    bool* /*usingScreen*/, bool* /*isMedia*/, uint64_t* /*audioFormat*/) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ct) {
        ctx->audioCt.store(*ct);
        if (ctx->ingress) {
            ctx->ingress->signalDiscontinuity(
                core::DiscontinuityReason::AudioFormatChanged);
        }
    }
}

void pmConnInit(void* cls) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx) {
        ctx->clock.reset();
        if (ctx->ingress) {
            ctx->ingress->signalDiscontinuity(
                core::DiscontinuityReason::TransportReconnect);
        }
    }
}

void pmConnReset(void* cls, int /*reason*/) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx) {
        ctx->clock.reset();
        if (ctx->ingress) {
            ctx->ingress->signalDiscontinuity(
                core::DiscontinuityReason::StreamRestart);
        }
    }
}

void pmConnDestroy(void* cls) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ctx->ingress) {
        ctx->ingress->signalDiscontinuity(
            core::DiscontinuityReason::StreamRestart);
    }
}

void pmVideoFlush(void* cls) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ctx->ingress) {
        ctx->ingress->signalDiscontinuity(
            core::DiscontinuityReason::DecoderReset);
    }
}

void pmAudioFlush(void* cls) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ctx->ingress) {
        ctx->ingress->signalDiscontinuity(
            core::DiscontinuityReason::DecoderReset);
    }
}

// Initial playback volume reported to the sender in the AirPlay /info reply.
// UxPlay's info handler (raop_handlers.h) calls this callback UNGUARDED while
// building "initialVolume", so it must be non-NULL or the first client request
// jumps through a null pointer and crashes the HTTP worker thread. AirPlay's
// volume scale is dB attenuation: 0.0 = full volume, -144.0 = muted. PadMirror
// renders audio at unity and exposes no volume control in V1, so report 0.0.
double pmAudioGetClientVolume(void* /*cls*/) {
    return 0.0;
}

// Client "heartbeat" received on the /feedback POST. UxPlay calls conn_feedback
// UNGUARDED (raop_handlers.h raop_handler_feedback), so leaving it NULL crashes
// the session the moment the iPad sends its periodic keep-alive. PadMirror has
// no per-heartbeat work in V1; accept it as a no-op so the session stays up.
void pmConnFeedback(void* /*cls*/) {
}

// Session/stream reset requested by the receiver core (teardown, shutdown,
// RTP<->HLS transitions). UxPlay calls video_reset UNGUARDED on several teardown
// paths (raop_handlers.h, http_handlers.h, raop.c, raop_rtp_mirror.c), so a NULL
// here crashes on client disconnect. Map every reset to a decoder-flush
// discontinuity: it drops any buffered frames and lets the pipeline recover
// cleanly whether the client is switching apps, stopping, or reconnecting.
void pmVideoReset(void* cls, reset_type_t /*reset_type*/) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ctx->ingress) {
        ctx->ingress->signalDiscontinuity(
            core::DiscontinuityReason::DecoderReset);
    }
}

// Log sink for the vendored UxPlay tree. logger_log() invokes this for every
// message at or below the configured level; if no callback is registered it
// would (in release, with assert() compiled out) dereference a NULL pointer and
// crash the HTTP worker thread on the first logged event. Registering a real
// sink both prevents that and surfaces AirPlay diagnostics on stderr.
void pmRaopLog(void* /*cls*/, int level, const char* msg) {
    if (!msg) return;
    std::fprintf(stderr, "[airplay:%d] %s\n", level, msg);
}

// Mirror video stream suspended/resumed. UxPlay calls these UNGUARDED from the
// mirror RTP thread (raop_rtp_mirror.c) both on the sender's suspend/resume
// signal and when a non-conforming HEVC parameter set forces a pause, so a NULL
// crashes mid-stream. Treat a pause as a decoder-flush point; on resume the
// next keyframe rebuilds state. PadMirror has no separate "paused" UI state in
// V1, so both simply flush rather than gate the pipeline.
void pmVideoPause(void* cls) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ctx->ingress) {
        ctx->ingress->signalDiscontinuity(
            core::DiscontinuityReason::DecoderReset);
    }
}

void pmVideoResume(void* cls) {
    auto* ctx = static_cast<RaopReceiverContext*>(cls);
    if (ctx && ctx->ingress) {
        ctx->ingress->signalDiscontinuity(
            core::DiscontinuityReason::DecoderReset);
    }
}

} // extern "C"

} // namespace

RaopReceiver::RaopReceiver(core::IMediaIngress& mediaIngress)
    : mediaIngress_(mediaIngress) {}

RaopReceiver::~RaopReceiver() {
    stop();
}

bool RaopReceiver::running() const {
    std::lock_guard lock(mutex_);
    return running_;
}


// ---- lifecycle -------------------------------------------------------------

RaopReceiver::StartResult RaopReceiver::start(const std::string& receiverName) {
    std::lock_guard lock(mutex_);
    if (running_) {
        return {true, "AirPlay receiver already running", port_};
    }

    context_ = std::make_shared<RaopReceiverContext>();
    context_->ingress = &mediaIngress_;
    context_->active.store(true);

    // UxPlay's main() calls this once to initialize the Windows QPC frequency
    // used by the NTP timing sessions. PadMirror does not run that main(), so we
    // must call it before any RAOP session is created. Idempotent.
    ntp_global_init();

    raop_callbacks_t cbs;
    std::memset(&cbs, 0, sizeof(cbs));
    cbs.cls = context_.get();
    cbs.video_process = &pmVideoProcess;
    cbs.audio_process = &pmAudioProcess;
    cbs.video_set_codec = &pmVideoSetCodec;
    cbs.audio_get_format = &pmAudioGetFormat;
    cbs.conn_init = &pmConnInit;
    cbs.conn_reset = &pmConnReset;
    cbs.conn_destroy = &pmConnDestroy;
    cbs.video_flush = &pmVideoFlush;
    cbs.audio_flush = &pmAudioFlush;
    cbs.audio_set_client_volume = &pmAudioGetClientVolume;
    cbs.conn_feedback = &pmConnFeedback;
    cbs.video_reset = &pmVideoReset;
    cbs.video_pause = &pmVideoPause;
    cbs.video_resume = &pmVideoResume;

    raop_t* raop = raop_init(&cbs);
    if (!raop) {
        releaseLocked();
        return {false, "raop_init failed", 0};
    }
    raop_ = raop;

    // Register a log sink before any RAOP call can log. Without it, logger_log
    // would call a NULL callback (assert is compiled out in release) and crash
    // the HTTP worker on the first message. LOGGER_WARNING keeps the volume of
    // per-request logging low while still surfacing problems.
    raop_set_log_callback(raop, &pmRaopLog, nullptr);
    raop_set_log_level(raop, LOGGER_WARNING);

    const std::string mac = stableMac();
    // nohold=1 lets a new client take over capture; keyfile="" = ephemeral key.
    if (raop_init2(raop, 1, mac.c_str(), "")) {
        releaseLocked();
        return {false, "raop_init2 failed", 0};
    }

    // Advertise the mirror display capabilities the iPad negotiates against in
    // its /info reply. The vendored default caps maxFPS at 30, which makes the
    // iPad throttle its screen-mirroring stream to 30 fps regardless of what the
    // app can render. Raise the cap to 60 so the iPad may deliver real 60 fps
    // frames (this is a genuine source-rate change, not interpolation: PadMirror
    // still renders exactly the frames the iPad sends). refreshRate=60 and a
    // 1920x1080 canvas match the plan's output contract.
    raop_set_plist(raop, "width", 1920);
    raop_set_plist(raop, "height", 1080);
    raop_set_plist(raop, "refreshRate", 60);
    raop_set_plist(raop, "maxFPS", 60);

    // Bind the RTSP/HTTP listener first; port 0 => dynamically assigned.
    // raop_start_httpd returns 1 on success (httpd_start convention), <=0 on
    // failure.
    unsigned short raopPort = 0;
    if (raop_start_httpd(raop, &raopPort) != 1 || raopPort == 0) {
        releaseLocked();
        return {false, "raop_start_httpd failed to bind listener", 0};
    }
    raop_set_port(raop, raopPort);
    port_ = static_cast<std::uint16_t>(raopPort);

    // Build service-discovery identity from the same MAC (hw_addr = 6 bytes).
    unsigned char hw[6] = {0};
    {
        unsigned int v[6] = {0};
        std::sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
                    &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
        for (int i = 0; i < 6; ++i) hw[i] = static_cast<unsigned char>(v[i]);
    }

    int dnssdError = 0;
    const std::string name = receiverName.empty() ? "PadMirror" : receiverName;
    dnssd_t* dnssd = dnssd_init(
        name.c_str(), static_cast<int>(name.size()),
        reinterpret_cast<char*>(hw), sizeof(hw), 0, &dnssdError);
    if (!dnssd || dnssdError) {
        releaseLocked();
        return {false, "dnssd_init failed (mDNS unavailable)", 0};
    }
    dnssd_ = dnssd;

    // Advertise mirroring + audio capability. Defaults from dnssdint.h already
    // enable the core mirror feature bits; explicitly set the ones that matter.
    dnssd_set_airplay_features(dnssd, 7, 1);   // mirroring supported
    dnssd_set_airplay_features(dnssd, 9, 1);   // audio supported
    dnssd_set_airplay_features(dnssd, 30, 1);  // RAOP support advertised on _airplay

    raop_set_dnssd(raop, dnssd);

    // Publish records LAST, only after the listener bound. This is the point at
    // which the iPad can discover PadMirror in the AirPlay picker.
    if (dnssd_register_raop(dnssd, raopPort) != 0) {
        releaseLocked();
        return {false, "dnssd_register_raop failed", 0};
    }
    raopRegistered_ = true;
    if (dnssd_register_airplay(dnssd, raopPort) != 0) {
        releaseLocked();
        return {false, "dnssd_register_airplay failed", 0};
    }
    airplayRegistered_ = true;

    running_ = true;
    return {true, "AirPlay receiver publishing _raop._tcp/_airplay._tcp", port_};
}

void RaopReceiver::stop() {
    std::shared_ptr<RaopReceiverContext> ctx;
    {
        std::lock_guard lock(mutex_);
        ctx = context_;
        if (ctx) ctx->active.store(false);
        releaseLocked();
        running_ = false;
    }
    // Context is released after releaseLocked() has torn down raop (which joins
    // UxPlay worker threads), so no callback can still reference it.
    ctx.reset();
}

// Releases native resources in reverse registration order. Must hold mutex_.
void RaopReceiver::releaseLocked() {
    auto* dnssd = static_cast<dnssd_t*>(dnssd_);
    if (dnssd) {
        if (airplayRegistered_) dnssd_unregister_airplay(dnssd);
        if (raopRegistered_) dnssd_unregister_raop(dnssd);
    }
    airplayRegistered_ = false;
    raopRegistered_ = false;

    if (raop_) {
        // raop_destroy stops the RTSP server and joins its worker threads.
        raop_destroy(static_cast<raop_t*>(raop_));
        raop_ = nullptr;
    }
    if (dnssd) {
        dnssd_destroy(dnssd);
        dnssd_ = nullptr;
    }
    context_.reset();
    port_ = 0;
}

} // namespace padmirror::airplay
