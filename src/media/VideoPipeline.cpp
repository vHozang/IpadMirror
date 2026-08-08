#include "media/VideoPipeline.h"

#include "app/Settings.h"
#include "diagnostics/Metrics.h"
#include "platform/PlatformVideo.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <algorithm>
#include <utility>

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

struct ProbeContext {
    diagnostics::Metrics* metrics = nullptr;
    bool decoded = false;
};

GstPadProbeReturn metricsProbe(GstPad*, GstPadProbeInfo* info, gpointer userData) {
    if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) != 0) {
        const auto* context = static_cast<ProbeContext*>(userData);
        if (context->metrics) {
            context->decoded ? context->metrics->decodedFrame() : context->metrics->renderedFrame();
        }
    }
    return GST_PAD_PROBE_OK;
}

void freeProbeContext(gpointer data) {
    delete static_cast<ProbeContext*>(data);
}

void queueOverrun(GstElement*, gpointer userData) {
    auto* metrics = static_cast<diagnostics::Metrics*>(userData);
    if (metrics) metrics->droppedFrame();
}

} // namespace

VideoPipeline::~VideoPipeline() {
    stop();
}

bool VideoPipeline::start(
    const app::Settings& settings,
    capture::VideoCodec codec,
    std::uintptr_t windowHandle,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();
    auto backend = platform::videoBackend();
    if (codec == capture::VideoCodec::Hevc) {
#ifdef Q_OS_WIN
        backend.decoderCandidates = {
            "d3d11h265dec",
            "d3d11h265device1dec",
            "nvh265dec",
            "avdec_h265",
        };
#elif defined(Q_OS_MACOS)
        backend.decoderCandidates = {"vtdec_hw", "vtdec", "avdec_h265"};
#else
        backend.decoderCandidates = {"vah265dec", "vaapih265dec", "avdec_h265"};
#endif
    }
    const auto requested = settings.hardwareDecoder().toStdString();
    if (requested != "Auto") {
        if (requested == "D3D11") {
            backend.decoderCandidates = {
                codec == capture::VideoCodec::Hevc ? "d3d11h265dec" : "d3d11h264dec"
            };
        }
        else if (requested == "VideoToolbox") backend.decoderCandidates = {"vtdec_hw", "vtdec"};
        else backend.decoderCandidates.insert(backend.decoderCandidates.begin(), requested);
    }

    const auto decoderName = chooseFactory(backend.decoderCandidates);
    const auto sinkName = chooseFactory(backend.sinkCandidates);
    if (decoderName.empty() || sinkName.empty()) {
        if (errorHandler) {
            errorHandler(decoderName.empty()
                ? (codec == capture::VideoCodec::Hevc
                    ? "HEVC decoder is unavailable"
                    : "Hardware H.264 decoder is unavailable")
                : "GPU video sink is unavailable");
        }
        return false;
    }

    auto* pipeline = gst_pipeline_new("padmirror-usb-video");
    auto* source = gst_element_factory_make("appsrc", "usb-video-source");
    auto* queue = gst_element_factory_make("queue", "live-video-queue");
    const auto* parserFactory = codec == capture::VideoCodec::Hevc ? "h265parse" : "h264parse";
    auto* parser = gst_element_factory_make(parserFactory, "video-parser");
    auto* decoder = gst_element_factory_make(decoderName.c_str(), "hardware-video-decoder");
    auto* renderQueue = gst_element_factory_make("queue", "render-video-queue");
    auto* sink = gst_element_factory_make(sinkName.c_str(), "video-output");
    if (!pipeline || !source || !queue || !parser || !decoder || !renderQueue || !sink) {
        if (pipeline) gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot create the GStreamer video pipeline");
        return false;
    }

    auto* caps = gst_caps_new_simple(
        codec == capture::VideoCodec::Hevc ? "video/x-h265" : "video/x-h264",
        "stream-format", G_TYPE_STRING, "byte-stream",
        "alignment", G_TYPE_STRING, "au",
        nullptr);
    g_object_set(
        source,
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "block", FALSE,
        "caps", caps,
        nullptr);
    gst_caps_unref(caps);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "max-buffers")) {
        g_object_set(source, "max-buffers", static_cast<guint64>(2), nullptr);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "leaky-type")) {
        g_object_set(source, "leaky-type", 2, nullptr);
    }

    g_object_set(
        queue,
        "max-size-buffers", 1,
        "max-size-bytes", 0,
        "max-size-time", static_cast<guint64>(0),
        "leaky", 2,
        nullptr);
    g_object_set(
        renderQueue,
        "max-size-buffers", 1,
        "max-size-bytes", 0,
        "max-size-time", static_cast<guint64>(0),
        "leaky", 2,
        nullptr);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(parser), "config-interval")) {
        g_object_set(parser, "config-interval", -1, nullptr);
    }
    setBooleanIfPresent(sink, "sync", FALSE);
    setBooleanIfPresent(sink, "qos", TRUE);
    setBooleanIfPresent(sink, "force-aspect-ratio", settings.maintainAspectRatio());

    gst_bin_add_many(GST_BIN(pipeline), source, queue, parser, decoder, renderQueue, sink, nullptr);
    if (!gst_element_link_many(source, queue, parser, decoder, renderQueue, sink, nullptr)) {
        gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot link the zero-copy video pipeline");
        return false;
    }

    if (auto* pad = gst_element_get_static_pad(decoder, "src")) {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            metricsProbe,
            new ProbeContext{metrics, true},
            freeProbeContext);
        gst_object_unref(pad);
    }
    if (auto* pad = gst_element_get_static_pad(sink, "sink")) {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            metricsProbe,
            new ProbeContext{metrics, false},
            freeProbeContext);
        gst_object_unref(pad);
    }
    g_signal_connect(queue, "overrun", G_CALLBACK(queueOverrun), metrics);
    g_signal_connect(renderQueue, "overrun", G_CALLBACK(queueOverrun), metrics);

    {
        std::lock_guard lock(mutex_);
        pipeline_ = pipeline;
        appSource_ = source;
        sink_ = sink;
        metrics_ = metrics;
        errorHandler_ = std::move(errorHandler);
        windowHandle_ = windowHandle;
        codec_ = codec;
        if (windowHandle_ != 0 && GST_IS_VIDEO_OVERLAY(sink_)) {
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink_), static_cast<guintptr>(windowHandle_));
        }
    }

    const auto stateResult = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (stateResult == GST_STATE_CHANGE_FAILURE) {
        stop();
        return false;
    }
    busStopRequested_.store(false);
    busThread_ = std::thread([this] { monitorBus(); });
    return true;
}

void VideoPipeline::stop() {
    busStopRequested_.store(true);
    if (busThread_.joinable()) busThread_.join();

    std::lock_guard lock(mutex_);
    if (!pipeline_) return;
    if (appSource_) gst_app_src_end_of_stream(GST_APP_SRC(appSource_));
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appSource_ = nullptr;
    sink_ = nullptr;
    metrics_ = nullptr;
}

bool VideoPipeline::push(capture::VideoPacket packet) {
    std::lock_guard lock(mutex_);
    if (!appSource_ || packet.data.empty()) return false;
    auto* buffer = gst_buffer_new_allocate(nullptr, packet.data.size(), nullptr);
    if (!buffer) return false;
    gst_buffer_fill(buffer, 0, packet.data.data(), packet.data.size());
    GST_BUFFER_PTS(buffer) = packet.ptsNs == 0 ? GST_CLOCK_TIME_NONE : packet.ptsNs;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = GST_SECOND / 60;
    if (!packet.keyFrame) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    if (metrics_) metrics_->sourceFrame();
    const auto flow = gst_app_src_push_buffer(GST_APP_SRC(appSource_), buffer);
    return flow == GST_FLOW_OK;
}

void VideoPipeline::setWindowHandle(std::uintptr_t windowHandle) {
    std::lock_guard lock(mutex_);
    windowHandle_ = windowHandle;
    if (sink_ && windowHandle_ != 0 && GST_IS_VIDEO_OVERLAY(sink_)) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink_), static_cast<guintptr>(windowHandle_));
    }
}

bool VideoPipeline::running() const {
    std::lock_guard lock(mutex_);
    return pipeline_ != nullptr;
}

void VideoPipeline::monitorBus() {
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
            if (handler) handler(error ? error->message : "GStreamer video error");
            if (error) g_error_free(error);
            g_free(debug);
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
}

} // namespace padmirror::media
