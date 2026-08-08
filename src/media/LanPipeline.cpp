#include "media/LanPipeline.h"

#include "app/Settings.h"
#include "diagnostics/Metrics.h"
#include "platform/PlatformAudio.h"
#include "platform/PlatformVideo.h"

#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

namespace padmirror::media {
namespace {

std::string chooseFactory(const std::vector<std::string>& candidates) {
    for (const auto& candidate : candidates) {
        if (auto* factory = gst_element_factory_find(candidate.c_str())) {
            gst_object_unref(factory);
            return candidate;
        }
    }
    return {};
}

void setBooleanIfPresent(GstElement* element, const char* property, gboolean value) {
    if (element && g_object_class_find_property(G_OBJECT_GET_CLASS(element), property)) {
        g_object_set(element, property, value, nullptr);
    }
}

GstElement* createAudioSink(const QString& selectedName, QString& actualName) {
    if (!selectedName.isEmpty()) {
        auto* monitor = gst_device_monitor_new();
        gst_device_monitor_add_filter(monitor, "Audio/Sink", nullptr);
        if (gst_device_monitor_start(monitor)) {
            auto* devices = gst_device_monitor_get_devices(monitor);
            for (auto* item = devices; item; item = item->next) {
                auto* device = GST_DEVICE(item->data);
                auto* display = gst_device_get_display_name(device);
                const auto displayName = QString::fromUtf8(display ? display : "");
                g_free(display);
                if (displayName == selectedName) {
                    auto* sink = gst_device_create_element(device, "lan-audio-output");
                    actualName = displayName;
                    g_list_free_full(devices, gst_object_unref);
                    gst_device_monitor_stop(monitor);
                    gst_object_unref(monitor);
                    return sink;
                }
            }
            g_list_free_full(devices, gst_object_unref);
            gst_device_monitor_stop(monitor);
        }
        gst_object_unref(monitor);
    }
    const auto backend = platform::audioBackend();
    const auto factory = chooseFactory(backend.sinkCandidates);
    actualName = QString::fromStdString(backend.displayName);
    return factory.empty() ? nullptr : gst_element_factory_make(factory.c_str(), "lan-audio-output");
}

struct ProbeContext {
    diagnostics::Metrics* metrics = nullptr;
    enum class Stage { Source, Decode, Render } stage = Stage::Source;
};

GstPadProbeReturn metricsProbe(GstPad*, GstPadProbeInfo* info, gpointer userData) {
    if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0) return GST_PAD_PROBE_OK;
    const auto* context = static_cast<ProbeContext*>(userData);
    if (!context->metrics) return GST_PAD_PROBE_OK;
    switch (context->stage) {
    case ProbeContext::Stage::Source: context->metrics->sourceFrame(); break;
    case ProbeContext::Stage::Decode: context->metrics->decodedFrame(); break;
    case ProbeContext::Stage::Render: context->metrics->renderedFrame(); break;
    }
    return GST_PAD_PROBE_OK;
}

void freeProbeContext(gpointer data) { delete static_cast<ProbeContext*>(data); }

void addProbe(GstElement* element, const char* padName, diagnostics::Metrics* metrics, ProbeContext::Stage stage) {
    if (auto* pad = gst_element_get_static_pad(element, padName)) {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            metricsProbe,
            new ProbeContext{metrics, stage},
            freeProbeContext);
        gst_object_unref(pad);
    }
}

void queueOverrun(GstElement*, gpointer userData) {
    auto* metrics = static_cast<diagnostics::Metrics*>(userData);
    if (metrics) metrics->droppedFrame();
}

} // namespace

LanPipeline::~LanPipeline() {
    stop();
}

bool LanPipeline::start(
    const app::Settings& settings,
    std::uint16_t videoPort,
    std::uint16_t audioPort,
    std::uintptr_t windowHandle,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();
    auto videoBackend = platform::videoBackend();
    const auto decoderName = chooseFactory(videoBackend.decoderCandidates);
    const auto videoSinkName = chooseFactory(videoBackend.sinkCandidates);
    QString actualAudioName;
    auto* audioSink = createAudioSink(settings.audioDevice(), actualAudioName);
    if (decoderName.empty() || videoSinkName.empty() || !audioSink) {
        if (errorHandler) errorHandler("AirPlay media backends are unavailable");
        if (audioSink) gst_object_unref(audioSink);
        return false;
    }

    auto* pipeline = gst_pipeline_new("padmirror-airplay-lan");
    auto* videoSource = gst_element_factory_make("udpsrc", "airplay-video-source");
    auto* videoIngressQueue = gst_element_factory_make("queue", "airplay-video-ingress");
    auto* videoDepay = gst_element_factory_make("rtph264depay", "airplay-video-depay");
    auto* parser = gst_element_factory_make("h264parse", "airplay-h264-parser");
    auto* decoder = gst_element_factory_make(decoderName.c_str(), "airplay-video-decoder");
    auto* videoQueue = gst_element_factory_make("queue", "airplay-live-video-queue");
    auto* videoSink = gst_element_factory_make(videoSinkName.c_str(), "airplay-video-output");

    auto* audioSource = gst_element_factory_make("udpsrc", "airplay-audio-source");
    auto* audioDepay = gst_element_factory_make("rtpL16depay", "airplay-audio-depay");
    auto* audioConvert = gst_element_factory_make("audioconvert", "airplay-audio-convert");
    auto* audioResample = gst_element_factory_make("audioresample", "airplay-audio-resample");
    auto* audioCapsFilter = gst_element_factory_make("capsfilter", "airplay-audio-48khz");
    auto* audioQueue = gst_element_factory_make("queue", "airplay-live-audio-queue");

    if (!pipeline || !videoSource || !videoIngressQueue || !videoDepay || !parser || !decoder ||
        !videoQueue || !videoSink || !audioSource || !audioDepay || !audioConvert ||
        !audioResample || !audioCapsFilter || !audioQueue) {
        if (pipeline) gst_object_unref(pipeline);
        else gst_object_unref(audioSink);
        if (errorHandler) errorHandler("Cannot create the AirPlay RTP pipeline");
        return false;
    }

    auto* videoCaps = gst_caps_new_simple(
        "application/x-rtp",
        "media", G_TYPE_STRING, "video",
        "clock-rate", G_TYPE_INT, 90000,
        "encoding-name", G_TYPE_STRING, "H264",
        "payload", G_TYPE_INT, 96,
        nullptr);
    auto* audioCaps = gst_caps_new_simple(
        "application/x-rtp",
        "media", G_TYPE_STRING, "audio",
        "clock-rate", G_TYPE_INT, 44100,
        "encoding-name", G_TYPE_STRING, "L16",
        "channels", G_TYPE_INT, 2,
        "payload", G_TYPE_INT, 96,
        nullptr);
    g_object_set(
        videoSource,
        "port", static_cast<gint>(videoPort),
        "buffer-size", 16 * 1024 * 1024,
        "caps", videoCaps,
        nullptr);
    g_object_set(audioSource, "port", static_cast<gint>(audioPort), "caps", audioCaps, nullptr);
    gst_caps_unref(videoCaps);
    gst_caps_unref(audioCaps);

    auto* audioOutputCaps = gst_caps_new_simple(
        "audio/x-raw",
        "format", G_TYPE_STRING, "S16LE",
        "layout", G_TYPE_STRING, "interleaved",
        "rate", G_TYPE_INT, 48000,
        "channels", G_TYPE_INT, 2,
        nullptr);
    g_object_set(audioCapsFilter, "caps", audioOutputCaps, nullptr);
    gst_caps_unref(audioOutputCaps);

    // UxPlay forwards RTP over localhost. Drain the UDP socket on its own thread so
    // decoder or GPU stalls cannot overflow the kernel buffer during large keyframes.
    g_object_set(
        videoIngressQueue,
        "max-size-buffers", 0,
        "max-size-bytes", 8 * 1024 * 1024,
        "max-size-time", static_cast<guint64>(150) * GST_MSECOND,
        "leaky", 0,
        nullptr);
    setBooleanIfPresent(videoDepay, "wait-for-keyframe", TRUE);
    setBooleanIfPresent(videoDepay, "request-keyframe", TRUE);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(parser), "config-interval")) {
        g_object_set(parser, "config-interval", -1, nullptr);
    }
    setBooleanIfPresent(parser, "disable-passthrough", TRUE);
    setBooleanIfPresent(decoder, "discard-corrupted-frames", TRUE);
    g_object_set(
        videoQueue,
        "max-size-buffers", 3,
        "max-size-bytes", 0,
        "max-size-time", static_cast<guint64>(50) * GST_MSECOND,
        "leaky", 2,
        nullptr);
    const auto airplayAudioBufferMs = std::max(settings.audioBufferMs(), 40);
    g_object_set(
        audioQueue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", static_cast<guint64>(airplayAudioBufferMs) * GST_MSECOND,
        "leaky", 0,
        nullptr);
    setBooleanIfPresent(videoSink, "sync", FALSE);
    setBooleanIfPresent(videoSink, "async", FALSE);
    setBooleanIfPresent(videoSink, "qos", FALSE);
    setBooleanIfPresent(videoSink, "enable-last-sample", TRUE);
    setBooleanIfPresent(videoSink, "force-aspect-ratio", settings.maintainAspectRatio());
    setBooleanIfPresent(audioSink, "sync", FALSE);
    setBooleanIfPresent(audioSink, "low-latency", FALSE);
    setBooleanIfPresent(audioSink, "exclusive", FALSE);

    gst_bin_add_many(
        GST_BIN(pipeline),
        videoSource, videoIngressQueue, videoDepay, parser, decoder, videoQueue, videoSink,
        audioSource, audioDepay, audioConvert, audioResample, audioCapsFilter, audioQueue, audioSink,
        nullptr);
    const bool videoLinked = gst_element_link_many(
        videoSource, videoIngressQueue, videoDepay, parser, decoder, videoQueue, videoSink, nullptr);
    // UxPlay already decodes and packetizes PCM over localhost; an RTP jitter buffer here
    // causes audible drops when the source switches between AAC-ELD and ALAC sessions.
    const bool audioLinked = gst_element_link_many(
        audioSource, audioDepay, audioConvert, audioResample, audioCapsFilter,
        audioQueue, audioSink, nullptr);
    if (!videoLinked || !audioLinked) {
        gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot link the AirPlay low-latency pipeline");
        return false;
    }

    addProbe(videoDepay, "src", metrics, ProbeContext::Stage::Source);
    addProbe(decoder, "src", metrics, ProbeContext::Stage::Decode);
    addProbe(videoSink, "sink", metrics, ProbeContext::Stage::Render);
    g_signal_connect(videoQueue, "overrun", G_CALLBACK(queueOverrun), metrics);

    {
        std::lock_guard lock(mutex_);
        pipeline_ = pipeline;
        videoSink_ = videoSink;
        metrics_ = metrics;
        errorHandler_ = std::move(errorHandler);
        windowHandle_ = windowHandle;
        if (windowHandle_ != 0 && GST_IS_VIDEO_OVERLAY(videoSink_)) {
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink_), static_cast<guintptr>(windowHandle_));
        }
    }
    if (metrics) metrics->setAudioBackend(actualAudioName);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stop();
        return false;
    }
    busStopRequested_.store(false);
    busThread_ = std::thread([this] { monitorBus(); });
    return true;
}

void LanPipeline::stop() {
    busStopRequested_.store(true);
    if (busThread_.joinable()) busThread_.join();
    std::lock_guard lock(mutex_);
    if (!pipeline_) return;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    videoSink_ = nullptr;
    metrics_ = nullptr;
}

void LanPipeline::setWindowHandle(std::uintptr_t windowHandle) {
    std::lock_guard lock(mutex_);
    windowHandle_ = windowHandle;
    if (videoSink_ && windowHandle_ != 0 && GST_IS_VIDEO_OVERLAY(videoSink_)) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink_), static_cast<guintptr>(windowHandle_));
    }
}

bool LanPipeline::running() const {
    std::lock_guard lock(mutex_);
    return pipeline_ != nullptr;
}

void LanPipeline::monitorBus() {
    GstBus* bus = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (pipeline_) bus = gst_element_get_bus(pipeline_);
    }
    if (!bus) return;
    while (!busStopRequested_.load()) {
        auto* message = gst_bus_timed_pop_filtered(
            bus,
            100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (!message) continue;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            ErrorHandler handler;
            {
                std::lock_guard lock(mutex_);
                handler = errorHandler_;
            }
            if (handler) handler(error ? error->message : "AirPlay GStreamer error");
            if (error) g_error_free(error);
            g_free(debug);
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
}

} // namespace padmirror::media
