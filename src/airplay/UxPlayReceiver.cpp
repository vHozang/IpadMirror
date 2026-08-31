#include "airplay/UxPlayReceiver.h"

#if defined(_MSC_VER)
#include <BaseTsd.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#endif

extern "C" {
#include "audio_renderer.h"
#include "dnssd.h"
#include "logger.h"
#include "raop.h"
#include "stream.h"
#include "video_renderer.h"
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace padmirror::airplay {

struct UxPlayReceiver::Context {
    StateCallback stateCallback;
    std::atomic<bool> active{false};
    std::atomic<int> openConnections{0};
    std::atomic<std::uint64_t> remoteClockOffset{0};
    std::atomic<std::uintptr_t> windowHandle{0};
    std::mutex rendererMutex;
    logger_t* renderLogger = nullptr;
    std::string receiverName;

    void publish(ReceiverState state, std::string detail) const {
        if (active.load(std::memory_order_acquire) && stateCallback) {
            stateCallback(state, detail);
        }
    }
};

namespace {

std::string randomMac() {
    std::random_device source;
    std::uniform_int_distribution<int> byte(0, 255);
    std::array<unsigned char, 6> bytes{};
    for (auto& value : bytes) {
        value = static_cast<unsigned char>(byte(source));
    }
    bytes[0] = static_cast<unsigned char>((bytes[0] & ~0x01U) | 0x02U);

    char text[18]{};
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return text;
}

std::string stableMac() {
    char host[256]{};
    if (gethostname(host, sizeof(host) - 1) != 0 || host[0] == '\0') {
        return randomMac();
    }

    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : std::string_view(host)) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }

    std::array<unsigned char, 6> bytes{};
    for (int index = 0; index < 6; ++index) {
        bytes[index] = static_cast<unsigned char>((hash >> (8 * index)) & 0xffU);
    }
    bytes[0] = static_cast<unsigned char>((bytes[0] & ~0x01U) | 0x02U);

    char text[18]{};
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return text;
}

std::array<char, 6> parseMac(const std::string& value) {
    std::array<char, 6> result{};
    unsigned int bytes[6]{};
    if (std::sscanf(value.c_str(), "%x:%x:%x:%x:%x:%x",
                    &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6) {
        for (int index = 0; index < 6; ++index) {
            result[index] = static_cast<char>(bytes[index]);
        }
    }
    return result;
}

void configureFeatures(dnssd_t* dnssd) {
    const int enabled[] = {1, 2, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15,
                           16, 17, 18, 19, 20, 21, 22, 25, 28, 30, 42};
    for (int bit = 0; bit < 64; ++bit) {
        dnssd_set_airplay_features(dnssd, bit, 0);
    }
    for (const int bit : enabled) {
        dnssd_set_airplay_features(dnssd, bit, 1);
    }
    // This build deliberately provides neither HLS nor legacy pairing.
    dnssd_set_airplay_features(dnssd, 0, 0);
    dnssd_set_airplay_features(dnssd, 4, 0);
    dnssd_set_airplay_features(dnssd, 27, 0);
}

void rendererLog(void*, int level, const char* message) {
    if (message) {
        std::fprintf(stderr, "[UxPlay renderer:%d] %s\n", level, message);
    }
}

void receiverLog(void*, int level, const char* message) {
    if (message) {
        std::fprintf(stderr, "[UxPlay receiver:%d] %s\n", level, message);
    }
}

void initializeVideoRenderer(UxPlayReceiver::Context* context) {
    videoflip_t flip[2] = {NONE, NONE};
    video_renderer_set_window_handle(context->windowHandle.load(std::memory_order_acquire));
    video_renderer_init(context->renderLogger,
                        context->receiverName.c_str(),
                        flip,
                        "h264parse",
                        "",
                        "d3d11h264dec",
                        "d3d11convert",
                        "d3d11videosink",
                        " force-aspect-ratio=true redraw-on-update=true",
                        false,
                        true,
                        true,
                        false,
                        3,
                        nullptr);
    video_renderer_start();
}

void resetVideoRenderer(UxPlayReceiver::Context* context) {
    if (!context || !context->active.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard lock(context->rendererMutex);
    video_renderer_stop();
    video_renderer_destroy();
    initializeVideoRenderer(context);
    context->remoteClockOffset.store(0, std::memory_order_release);
}

extern "C" {

void uxConnInit(void* cls) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (!context) {
        return;
    }
    context->openConnections.fetch_add(1, std::memory_order_acq_rel);
    context->publish(ReceiverState::Streaming, "AirPlay client connected");
}

void uxConnDestroy(void* cls) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (!context) {
        return;
    }
    const int previous = context->openConnections.fetch_sub(1, std::memory_order_acq_rel);
    if (previous <= 1) {
        context->openConnections.store(0, std::memory_order_release);
        context->remoteClockOffset.store(0, std::memory_order_release);
        std::lock_guard lock(context->rendererMutex);
        audio_renderer_stop();
        context->publish(ReceiverState::Waiting, "Ready for AirPlay");
    }
}

void uxConnFeedback(void*) {
}

void uxConnReset(void* cls, int reason) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (!context) {
        return;
    }
    context->remoteClockOffset.store(0, std::memory_order_release);
    if (reason == 1) {
        context->publish(ReceiverState::Error, "AirPlay client connection was lost");
    } else if (reason == 2) {
        context->publish(ReceiverState::Error, "HLS video is not supported");
    }
}

void uxAudioProcess(void* cls, raop_ntp_t*, audio_decode_struct* data) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (!context || !data || !data->data || data->data_len <= 0) {
        return;
    }

    std::uint64_t offset = context->remoteClockOffset.load(std::memory_order_acquire);
    if (!offset) {
        const std::uint64_t local = data->ntp_time_local ? data->ntp_time_local : get_local_time();
        offset = local - data->ntp_time_remote;
        context->remoteClockOffset.store(offset, std::memory_order_release);
    }
    data->ntp_time_remote += offset;
    audio_renderer_render_buffer(data->data, &data->data_len, &data->seqnum,
                                 &data->ntp_time_remote);
}

void uxVideoProcess(void* cls, raop_ntp_t*, video_decode_struct* data) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (!context || !data || !data->data || data->data_len <= 0) {
        return;
    }

    std::uint64_t offset = context->remoteClockOffset.load(std::memory_order_acquire);
    if (!offset) {
        const std::uint64_t local = data->ntp_time_local ? data->ntp_time_local : get_local_time();
        offset = local - data->ntp_time_remote;
        context->remoteClockOffset.store(offset, std::memory_order_release);
    }

    int attempts = 0;
    std::uint64_t mismatch = 0;
    do {
        data->ntp_time_remote += offset;
        mismatch = video_renderer_render_buffer(data->data, &data->data_len,
                                                &data->nal_count, &data->ntp_time_remote);
        if (mismatch) {
            offset += mismatch;
            context->remoteClockOffset.store(offset, std::memory_order_release);
        }
        ++attempts;
    } while (mismatch && attempts < 10);
}

void uxVideoPause(void*) {
    video_renderer_pause();
}

void uxVideoResume(void*) {
    video_renderer_resume();
}

void uxAudioFlush(void*) {
    audio_renderer_flush();
}

void uxVideoFlush(void*) {
    video_renderer_flush();
}

double uxInitialVolume(void*) {
    return 0.0;
}

void uxAudioSetVolume(void*, float volume) {
    double fraction = 0.0;
    if (volume == -144.0F || volume <= -30.0F) {
        fraction = 0.0;
    } else if (volume >= 0.0F) {
        fraction = 1.0;
    } else {
        fraction = static_cast<double>(30.0F + volume) / 30.0;
    }
    const double gain = fraction == 0.0 ? 0.0 : std::pow(10.0, 0.05 * (-30.0 + 30.0 * fraction));
    audio_renderer_set_volume(gain);
}

void uxAudioMetadata(void*, const void*, int) {
}

void uxAudioCoverArt(void*, const void*, int) {
}

void uxAudioStopCoverArt(void*) {
}

void uxAudioRemoteControl(void*, const char*, const char*) {
}

void uxAudioProgress(void*, std::uint32_t*, std::uint32_t*, std::uint32_t*) {
}

void uxAudioGetFormat(void*, unsigned char* compressionType, unsigned short*, bool*, bool*, std::uint64_t*) {
    if (compressionType) {
        audio_renderer_start(compressionType);
    }
}

void uxVideoReportSize(void*, float* sourceWidth, float* sourceHeight, float* width, float* height) {
    video_renderer_size(sourceWidth, sourceHeight, width, height);
}

void uxMirrorVideoRunning(void* cls, bool running) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (context) {
        context->publish(running ? ReceiverState::Streaming : ReceiverState::Waiting,
                         running ? "Mirroring iPad screen" : "Ready for AirPlay");
    }
}

void uxReportClient(void*, char*, char* model, char* name, bool* admit) {
    if (admit) {
        *admit = true;
    }
    video_renderer_set_device_model(model, name);
}

void uxDisplayPin(void*, char*) {
}

void uxRegisterClient(void*, const char*, const char*, const char*) {
}

bool uxCheckRegister(void*, const char*) {
    return true;
}

const char* uxPassword(void*, int* length) {
    if (length) {
        *length = 0;
    }
    return nullptr;
}

void uxExportDacp(void*, const char*, const char*) {
}

void uxVideoReset(void* cls, reset_type_t type) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (!context) {
        return;
    }
    context->remoteClockOffset.store(0, std::memory_order_release);
    switch (type) {
    case RESET_TYPE_NOHOLD:
    case RESET_TYPE_HLS_EOS:
        resetVideoRenderer(context);
        break;
    case RESET_TYPE_RTP_TO_HLS_TEARDOWN:
    case RESET_TYPE_RTP_SHUTDOWN:
    case RESET_TYPE_HLS_SHUTDOWN:
    case RESET_TYPE_ON_VIDEO_PLAY:
        std::lock_guard lock(context->rendererMutex);
        video_renderer_stop();
        break;
    }
}

int uxVideoSetCodec(void*, video_codec_t codec) {
    return video_renderer_choose_codec(false, codec == VIDEO_CODEC_H265);
}

void uxVideoPlay(void* cls, const char*, float) {
    auto* context = static_cast<UxPlayReceiver::Context*>(cls);
    if (context) {
        context->publish(ReceiverState::Error, "HLS video is not supported");
    }
}

void uxVideoScrub(void*, float) {
}

void uxVideoRate(void*, float rate) {
    if (rate == 1.0F) {
        video_renderer_resume();
    } else if (rate == 0.0F) {
        video_renderer_pause();
    }
}

void uxVideoStop(void*) {
}

void uxPlaybackInfo(void*, playback_info_t* info) {
    if (!info) {
        return;
    }
    std::memset(info, 0, sizeof(*info));
    info->position = -1.0;
    info->ready_to_play = false;
    info->playback_buffer_empty = true;
}

float uxPlaylistRemove(void*) {
    return -1.0F;
}

} // extern "C"

raop_callbacks_t callbacksFor(UxPlayReceiver::Context* context) {
    raop_callbacks_t callbacks{};
    callbacks.cls = context;
    callbacks.audio_process = &uxAudioProcess;
    callbacks.video_process = &uxVideoProcess;
    callbacks.video_pause = &uxVideoPause;
    callbacks.video_resume = &uxVideoResume;
    callbacks.conn_feedback = &uxConnFeedback;
    callbacks.conn_reset = &uxConnReset;
    callbacks.video_reset = &uxVideoReset;
    callbacks.conn_init = &uxConnInit;
    callbacks.conn_destroy = &uxConnDestroy;
    callbacks.audio_flush = &uxAudioFlush;
    callbacks.video_flush = &uxVideoFlush;
    callbacks.audio_set_client_volume = &uxInitialVolume;
    callbacks.audio_set_volume = &uxAudioSetVolume;
    callbacks.audio_set_metadata = &uxAudioMetadata;
    callbacks.audio_set_coverart = &uxAudioCoverArt;
    callbacks.audio_stop_coverart_rendering = &uxAudioStopCoverArt;
    callbacks.audio_remote_control_id = &uxAudioRemoteControl;
    callbacks.audio_set_progress = &uxAudioProgress;
    callbacks.audio_get_format = &uxAudioGetFormat;
    callbacks.video_report_size = &uxVideoReportSize;
    callbacks.mirror_video_running = &uxMirrorVideoRunning;
    callbacks.report_client_request = &uxReportClient;
    callbacks.display_pin = &uxDisplayPin;
    callbacks.register_client = &uxRegisterClient;
    callbacks.check_register = &uxCheckRegister;
    callbacks.passwd = &uxPassword;
    callbacks.export_dacp = &uxExportDacp;
    callbacks.video_set_codec = &uxVideoSetCodec;
    callbacks.on_video_play = &uxVideoPlay;
    callbacks.on_video_scrub = &uxVideoScrub;
    callbacks.on_video_rate = &uxVideoRate;
    callbacks.on_video_stop = &uxVideoStop;
    callbacks.on_video_acquire_playback_info = &uxPlaybackInfo;
    callbacks.on_video_playlist_remove = &uxPlaylistRemove;
    return callbacks;
}

} // namespace

UxPlayReceiver::UxPlayReceiver(StateCallback stateCallback)
    : stateCallback_(std::move(stateCallback)) {
}

UxPlayReceiver::~UxPlayReceiver() {
    stop();
}

UxPlayReceiver::StartResult UxPlayReceiver::start(const std::string& receiverName,
                                                  std::uintptr_t windowHandle) {
    std::lock_guard lock(mutex_);
    if (running_) {
        return {true, "AirPlay receiver already running", port_};
    }

    context_ = std::make_shared<Context>();
    context_->stateCallback = stateCallback_;
    context_->active.store(true, std::memory_order_release);
    context_->windowHandle.store(windowHandle, std::memory_order_release);
    context_->receiverName = receiverName.empty() ? "PadMirror" : receiverName;
    context_->publish(ReceiverState::Starting, "Starting UxPlay backend");

    ntp_global_init();
    if (!gstreamer_init()) {
        releaseLocked();
        return {false, "Required GStreamer plugins are unavailable", 0};
    }

    auto* renderLogger = logger_init();
    if (!renderLogger) {
        releaseLocked();
        return {false, "Could not initialize UxPlay renderer logging", 0};
    }
    renderLogger_ = renderLogger;
    context_->renderLogger = renderLogger;
    logger_set_callback(renderLogger, &rendererLog, nullptr);
    logger_set_level(renderLogger, LOGGER_INFO);

    const bool audioSync = false;
    const bool videoSync = true;
    audio_renderer_init(renderLogger, "wasapi2sink low-latency=true", &audioSync, &videoSync, "");
    initializeVideoRenderer(context_.get());
    renderersReady_ = true;

    raop_callbacks_t callbacks = callbacksFor(context_.get());
    auto* raop = raop_init(&callbacks);
    if (!raop) {
        releaseLocked();
        return {false, "raop_init failed", 0};
    }
    raop_ = raop;
    raop_set_log_callback(raop, &receiverLog, nullptr);
    raop_set_log_level(raop, LOGGER_INFO);

    const std::string mac = stableMac();
    if (raop_init2(raop, 1, mac.c_str(), "") != 0) {
        releaseLocked();
        return {false, "raop_init2 failed", 0};
    }

    raop_set_plist(raop, "width", 3840);
    raop_set_plist(raop, "height", 2160);
    raop_set_plist(raop, "refreshRate", 60);
    raop_set_plist(raop, "maxFPS", 60);
    raop_set_plist(raop, "hls", 0);

    unsigned short port = 0;
    if (raop_start_httpd(raop, &port) != 1 || port == 0) {
        releaseLocked();
        return {false, "UxPlay could not bind its RAOP listener", 0};
    }
    raop_set_port(raop, port);
    port_ = port;

    const auto hardwareAddress = parseMac(mac);
    int dnsError = 0;
    auto* dnssd = dnssd_init(context_->receiverName.c_str(),
                             static_cast<int>(context_->receiverName.size()),
                             hardwareAddress.data(),
                             static_cast<int>(hardwareAddress.size()),
                             0,
                             &dnsError);
    if (!dnssd || dnsError != 0) {
        releaseLocked();
        return {false, "mDNS initialization failed", 0};
    }
    dnssd_ = dnssd;
    configureFeatures(dnssd);
    raop_set_dnssd(raop, dnssd);

    // Service publication is intentionally last: clients never discover a dead listener.
    if (dnssd_register_raop(dnssd, port) != 0) {
        releaseLocked();
        return {false, "Publishing _raop._tcp failed", 0};
    }
    raopRegistered_ = true;
    if (dnssd_register_airplay(dnssd, port) != 0) {
        releaseLocked();
        return {false, "Publishing _airplay._tcp failed", 0};
    }
    airplayRegistered_ = true;

    running_ = true;
    context_->publish(ReceiverState::Waiting, "Ready for AirPlay");
    return {true, "UxPlay is listening and advertised", port_};
}

void UxPlayReceiver::setWindowHandle(std::uintptr_t windowHandle) {
    std::lock_guard lock(mutex_);
    if (context_) {
        context_->windowHandle.store(windowHandle, std::memory_order_release);
    }
    video_renderer_set_window_handle(windowHandle);
}

void UxPlayReceiver::stop() {
    std::lock_guard lock(mutex_);
    releaseLocked();
    running_ = false;
}

bool UxPlayReceiver::running() const {
    std::lock_guard lock(mutex_);
    return running_;
}

void UxPlayReceiver::releaseLocked() {
    if (context_) {
        context_->active.store(false, std::memory_order_release);
    }

    auto* dnssd = static_cast<dnssd_t*>(dnssd_);
    if (dnssd) {
        if (airplayRegistered_) {
            dnssd_unregister_airplay(dnssd);
        }
        if (raopRegistered_) {
            dnssd_unregister_raop(dnssd);
        }
    }
    airplayRegistered_ = false;
    raopRegistered_ = false;

    if (raop_) {
        raop_destroy(static_cast<raop_t*>(raop_));
        raop_ = nullptr;
    }
    if (dnssd) {
        dnssd_destroy(dnssd);
        dnssd_ = nullptr;
    }

    if (renderersReady_) {
        audio_renderer_destroy();
        video_renderer_destroy();
        renderersReady_ = false;
    }
    if (renderLogger_) {
        logger_destroy(static_cast<logger_t*>(renderLogger_));
        renderLogger_ = nullptr;
    }

    context_.reset();
    port_ = 0;
}

} // namespace padmirror::airplay
