#include "media/AudioPipeline.h"

#include "app/Settings.h"
#include "diagnostics/Metrics.h"
#include "platform/PlatformAudio.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <QVariantMap>

#include <algorithm>
#include <chrono>
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

GstElement* createSelectedAudioSink(const QString& selectedName, QString& actualName) {
    auto* monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Audio/Sink", nullptr);
    if (gst_device_monitor_start(monitor)) {
        auto* devices = gst_device_monitor_get_devices(monitor);
        for (auto* item = devices; item; item = item->next) {
            auto* device = GST_DEVICE(item->data);
            auto* display = gst_device_get_display_name(device);
            const auto displayName = QString::fromUtf8(display ? display : "");
            g_free(display);
            if (!selectedName.isEmpty() && displayName == selectedName) {
                auto* sink = gst_device_create_element(device, "audio-output");
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

    const auto backend = platform::audioBackend();
    const auto factory = chooseFactory(backend.sinkCandidates);
    actualName = QString::fromStdString(backend.displayName);
    return factory.empty() ? nullptr : gst_element_factory_make(factory.c_str(), "audio-output");
}

void setBooleanIfPresent(GstElement* element, const char* property, gboolean value) {
    if (element && g_object_class_find_property(G_OBJECT_GET_CLASS(element), property)) {
        g_object_set(element, property, value, nullptr);
    }
}

} // namespace


AudioPipeline::~AudioPipeline() {
    stop();
}

bool AudioPipeline::start(
    const app::Settings& settings,
    capture::AudioCodec codec,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();
    QString actualDevice;
    auto* sink = createSelectedAudioSink(settings.audioDevice(), actualDevice);
    auto* pipeline = gst_pipeline_new("padmirror-usb-audio");
    auto* source = gst_element_factory_make("appsrc", "usb-audio-source");
    auto* decoder = codec == capture::AudioCodec::AacEld
        ? gst_element_factory_make("avdec_aac", "aac-eld-decoder")
        : nullptr;
    auto* convert = gst_element_factory_make("audioconvert", "audio-convert");
    auto* resample = gst_element_factory_make("audioresample", "audio-resample");
    auto* capsFilter = gst_element_factory_make("capsfilter", "audio-48khz");
    auto* queue = gst_element_factory_make("queue", "live-audio-queue");
    if (!pipeline || !source || !convert || !resample || !capsFilter || !queue || !sink ||
        (codec == capture::AudioCodec::AacEld && !decoder)) {
        if (pipeline) gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot create the GStreamer audio pipeline");
        return false;
    }

    GstCaps* inputCaps = nullptr;
    if (codec == capture::AudioCodec::AacEld) {
        static constexpr std::uint8_t config[] = {0xF8, 0xE6, 0x40, 0x00};
        auto* codecData = gst_buffer_new_allocate(nullptr, sizeof(config), nullptr);
        gst_buffer_fill(codecData, 0, config, sizeof(config));
        inputCaps = gst_caps_new_simple(
            "audio/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "stream-format", G_TYPE_STRING, "raw",
            "rate", G_TYPE_INT, 48000,
            "channels", G_TYPE_INT, 2,
            "codec_data", GST_TYPE_BUFFER, codecData,
            nullptr);
        gst_buffer_unref(codecData);
    } else {
        inputCaps = gst_caps_new_simple(
            "audio/x-raw",
            "format", G_TYPE_STRING, "S16LE",
            "layout", G_TYPE_STRING, "interleaved",
            "rate", G_TYPE_INT, 48000,
            "channels", G_TYPE_INT, 2,
            nullptr);
    }
    auto* outputCaps = gst_caps_new_simple(
        "audio/x-raw",
        "format", G_TYPE_STRING, "S16LE",
        "layout", G_TYPE_STRING, "interleaved",
        "rate", G_TYPE_INT, 48000,
        "channels", G_TYPE_INT, 2,
        nullptr);
    g_object_set(
        source,
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "block", FALSE,
        "max-bytes", static_cast<guint64>(48000 * 2 * 2 * 40 / 1000),
        "caps", inputCaps,
        nullptr);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "max-buffers")) {
        g_object_set(source, "max-buffers", static_cast<guint64>(16), nullptr);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "leaky-type")) {
        g_object_set(source, "leaky-type", 2, nullptr);
    }
    g_object_set(capsFilter, "caps", outputCaps, nullptr);
    gst_caps_unref(inputCaps);
    gst_caps_unref(outputCaps);

    const auto queueLimit = static_cast<guint64>(settings.audioBufferMs()) * GST_MSECOND;
    g_object_set(
        queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", queueLimit,
        "leaky", 0,
        nullptr);
    strictSync_ = settings.strictSync();
    setBooleanIfPresent(sink, "sync", strictSync_ ? TRUE : FALSE);
    const bool safeMode = settings.audioMode() == app::Settings::AudioMode::Safe;
    const bool exclusive = settings.audioMode() == app::Settings::AudioMode::Exclusive;
    setBooleanIfPresent(sink, "low-latency", safeMode ? FALSE : TRUE);
    setBooleanIfPresent(sink, "exclusive", exclusive ? TRUE : FALSE);

    if (decoder) {
        gst_bin_add_many(GST_BIN(pipeline), source, decoder, convert, resample, capsFilter, queue, sink, nullptr);
    } else {
        gst_bin_add_many(GST_BIN(pipeline), source, convert, resample, capsFilter, queue, sink, nullptr);
    }
    const bool linked = decoder
        ? gst_element_link_many(source, decoder, convert, resample, capsFilter, queue, sink, nullptr)
        : gst_element_link_many(source, convert, resample, capsFilter, queue, sink, nullptr);
    if (!linked) {
        gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot link the low-latency audio pipeline");
        return false;
    }

    if (codec == capture::AudioCodec::PcmS16Le) {
        ringBuffer_ = std::make_unique<AudioRingBuffer>(
            48000, 2, 2, static_cast<double>(settings.audioBufferMs()), 40.0);
    }
    {
        std::lock_guard lock(mutex_);
        pipeline_ = pipeline;
        appSource_ = source;
        metrics_ = metrics;
        errorHandler_ = std::move(errorHandler);
        nextPtsNs_ = 0;
        ptsInitialized_ = false;
        unsupportedAudioReported_.store(false);
        codec_ = codec;
    }
    if (metrics) metrics->setAudioBackend(actualDevice);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stop();
        return false;
    }
    stopRequested_.store(false);
    if (codec == capture::AudioCodec::PcmS16Le) {
        feederThread_ = std::thread([this] { feed(); });
    }
    busThread_ = std::thread([this] { monitorBus(); });
    return true;
}

void AudioPipeline::stop() {
    stopRequested_.store(true);
    if (ringBuffer_) ringBuffer_->wakeAll();
    if (feederThread_.joinable()) feederThread_.join();
    if (busThread_.joinable()) busThread_.join();

    std::lock_guard lock(mutex_);
    if (pipeline_) {
        if (appSource_) gst_app_src_end_of_stream(GST_APP_SRC(appSource_));
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
    pipeline_ = nullptr;
    appSource_ = nullptr;
    metrics_ = nullptr;
    ringBuffer_.reset();
}

bool AudioPipeline::push(capture::AudioPacket packet) {
    if (packet.data.empty() || packet.format.codec != codec_) {
        bool expected = false;
        if (unsupportedAudioReported_.compare_exchange_strong(expected, true)) {
            ErrorHandler handler;
            {
                std::lock_guard lock(mutex_);
                handler = errorHandler_;
            }
            if (handler) handler("The iPad audio format does not match the active USB pipeline");
        }
        return false;
    }

    std::lock_guard lock(mutex_);
    if (codec_ == capture::AudioCodec::AacEld) {
        if (!appSource_) return false;
        auto* buffer = gst_buffer_new_allocate(nullptr, packet.data.size(), nullptr);
        if (!buffer) return false;
        gst_buffer_fill(buffer, 0, packet.data.data(), packet.data.size());
        GST_BUFFER_PTS(buffer) = packet.ptsNs == 0 ? GST_CLOCK_TIME_NONE : packet.ptsNs;
        GST_BUFFER_DURATION(buffer) = 10 * GST_MSECOND;
        const auto flow = gst_app_src_push_buffer(GST_APP_SRC(appSource_), buffer);
        return flow == GST_FLOW_OK;
    }
    if (!ringBuffer_) return false;
    if (!ptsInitialized_ && packet.ptsNs != 0) {
        nextPtsNs_ = packet.ptsNs;
        ptsInitialized_ = true;
    }
    const auto result = ringBuffer_->push(packet.data);
    if (result.droppedBytes != 0) {
        const auto frames = result.droppedBytes / 4;
        nextPtsNs_ += frames * GST_SECOND / 48000;
        if (metrics_) metrics_->audioResync();
    }
    if (metrics_) metrics_->setAudioBufferMs(ringBuffer_->bufferedMilliseconds());
    return true;
}

bool AudioPipeline::running() const {
    std::lock_guard lock(mutex_);
    return pipeline_ != nullptr;
}

QVariantList AudioPipeline::enumerateOutputDevices() {
    QVariantList result;
    QVariantMap defaultDevice;
    defaultDevice.insert(QStringLiteral("name"), QStringLiteral("System default"));
    result.push_back(defaultDevice);

    auto* monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Audio/Sink", nullptr);
    if (gst_device_monitor_start(monitor)) {
        auto* devices = gst_device_monitor_get_devices(monitor);
        for (auto* item = devices; item; item = item->next) {
            auto* device = GST_DEVICE(item->data);
            auto* display = gst_device_get_display_name(device);
            QVariantMap entry;
            entry.insert(QStringLiteral("name"), QString::fromUtf8(display ? display : ""));
            g_free(display);
            result.push_back(entry);
        }
        g_list_free_full(devices, gst_object_unref);
        gst_device_monitor_stop(monitor);
    }
    gst_object_unref(monitor);
    return result;
}

void AudioPipeline::feed() {
    constexpr std::size_t bytesPerFrame = 4;
    constexpr std::size_t framesPerChunk = 120; // 2.5 ms at 48 kHz.
    std::vector<std::uint8_t> chunk(bytesPerFrame * framesPerChunk);
    bool primed = false;
    bool wasUnderrun = false;

    while (!stopRequested_.load()) {
        AudioRingBuffer* ring = nullptr;
        {
            std::lock_guard lock(mutex_);
            ring = ringBuffer_.get();
        }
        if (!ring) break;
        const auto needed = primed ? chunk.size() : ring->targetBytes();
        if (!ring->waitForBytes(needed, std::chrono::milliseconds(10))) {
            if (primed && !wasUnderrun) {
                std::lock_guard lock(mutex_);
                if (metrics_) metrics_->audioUnderrun();
                wasUnderrun = true;
            }
            primed = false;
            continue;
        }
        primed = true;
        wasUnderrun = false;
        const auto popped = ring->pop(chunk);
        if (popped == 0) continue;

        GstElement* source = nullptr;
        std::uint64_t pts = 0;
        {
            std::lock_guard lock(mutex_);
            source = appSource_;
            pts = ptsInitialized_ ? nextPtsNs_ : 0;
            const auto frames = popped / bytesPerFrame;
            nextPtsNs_ += frames * GST_SECOND / 48000;
            if (metrics_) metrics_->setAudioBufferMs(ring->bufferedMilliseconds());
        }
        if (!source) break;

        auto* buffer = gst_buffer_new_allocate(nullptr, popped, nullptr);
        gst_buffer_fill(buffer, 0, chunk.data(), popped);
        GST_BUFFER_PTS(buffer) = pts == 0 ? GST_CLOCK_TIME_NONE : pts;
        GST_BUFFER_DURATION(buffer) = (popped / bytesPerFrame) * GST_SECOND / 48000;
        const auto flow = gst_app_src_push_buffer(GST_APP_SRC(source), buffer);
        if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING) break;
    }
}

void AudioPipeline::monitorBus() {
    GstBus* bus = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (pipeline_) bus = gst_element_get_bus(pipeline_);
    }
    if (!bus) return;
    while (!stopRequested_.load()) {
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
            if (handler) handler(error ? error->message : "GStreamer audio error");
            if (error) g_error_free(error);
            g_free(debug);
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
}

} // namespace padmirror::media
