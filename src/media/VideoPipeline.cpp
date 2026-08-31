#include "media/VideoPipeline.h"

#include <cstdio>

#include "app/Settings.h"
#include "diagnostics/Metrics.h"
#include "platform/PlatformVideo.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <algorithm>
#include <cstdlib>
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

void setInt64IfPresent(GstElement* element, const char* property, gint64 value) {
    if (element && g_object_class_find_property(G_OBJECT_GET_CLASS(element), property)) {
        g_object_set(element, property, value, nullptr);
    }
}

void setIntIfPresent(GstElement* element, const char* property, gint value) {
    if (element && g_object_class_find_property(G_OBJECT_GET_CLASS(element), property)) {
        g_object_set(element, property, value, nullptr);
    }
}

void setUnsigned64IfPresent(GstElement* element, const char* property, guint64 value) {
    if (element && g_object_class_find_property(G_OBJECT_GET_CLASS(element), property)) {
        g_object_set(element, property, value, nullptr);
    }
}

GstClockTime runningTime(GstElement* pipeline) {
    if (!pipeline) return 0;
    auto* clock = gst_element_get_clock(pipeline);
    if (!clock) return 0;
    const auto now = gst_clock_get_time(clock);
    const auto base = gst_element_get_base_time(pipeline);
    gst_object_unref(clock);
    return GST_CLOCK_TIME_IS_VALID(now) && GST_CLOCK_TIME_IS_VALID(base) && now > base
        ? now - base
        : 0;
}

struct ProbeContext {
    diagnostics::Metrics* metrics = nullptr;
    bool decoded = false;
    QString codec;
    int width = 0;
    int height = 0;
    bool loggedColorimetry = false;
};

GstPadProbeReturn metricsProbe(GstPad* pad, GstPadProbeInfo* info, gpointer userData) {
    if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }

    auto* context = static_cast<ProbeContext*>(userData);
    if (!context->metrics) return GST_PAD_PROBE_OK;

    const auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    const auto pts = buffer && GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buffer))
        ? GST_BUFFER_PTS(buffer)
        : GST_CLOCK_TIME_NONE;
    if (!context->decoded) {
        context->metrics->renderedFrame();
        return GST_PAD_PROBE_OK;
    }

    context->metrics->decodedFrame();
    if (GST_CLOCK_TIME_IS_VALID(pts)) context->metrics->videoTimestamp(pts);
    if (auto* caps = gst_pad_get_current_caps(pad)) {
        const auto* structure = gst_caps_get_structure(caps, 0);
        int width = 0;
        int height = 0;
        if (structure && gst_structure_get_int(structure, "width", &width) &&
            gst_structure_get_int(structure, "height", &height) &&
            (width != context->width || height != context->height)) {
            context->width = width;
            context->height = height;
            context->metrics->setVideoFormat(context->codec, width, height);
        }
        // Log the decoded colorimetry ONCE so we have ground truth for the
        // "image is ~30% brighter than the iPad" report: a limited-range (16-235)
        // stream shown as full-range (0-255) lifts the black level and washes the
        // picture out exactly that way. gst_caps_to_string prints the full
        // colorimetry/format so we can confirm the range before/after the fix.
        if (!context->loggedColorimetry) {
            gchar* capsStr = gst_caps_to_string(caps);
            std::fprintf(stderr, "[video-color] decoded caps: %s\n",
                         capsStr ? capsStr : "(null)");
            g_free(capsStr);
            context->loggedColorimetry = true;
        }
        gst_caps_unref(caps);
    }
    return GST_PAD_PROBE_OK;
}

void freeProbeContext(gpointer data) {
    delete static_cast<ProbeContext*>(data);
}

void queueOverrun(GstElement*, gpointer userData) {
    if (auto* metrics = static_cast<diagnostics::Metrics*>(userData)) {
        metrics->droppedFrame();
    }
}

} // namespace

VideoPipeline::~VideoPipeline() {
    stop();
}

bool VideoPipeline::start(
    const app::Settings& settings,
    core::VideoCodec codec,
    std::uintptr_t windowHandle,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();

    initialLeadNs_ = static_cast<std::uint64_t>(std::max(settings.audioBufferMs(), 5)) * GST_MSECOND;
    auto backend = platform::videoBackend();
    if (codec == core::VideoCodec::Hevc) {
        backend.decoderCandidates = {
            "d3d11h265dec",
            "d3d11h265device1dec",
            "nvh265dec",
            "avdec_h265",
        };
    }

    const auto requested = settings.hardwareDecoder().toStdString();
    if (requested != "Auto") {
        if (requested == "D3D11") {
            backend.decoderCandidates = {
                codec == core::VideoCodec::Hevc ? "d3d11h265dec" : "d3d11h264dec",
            };
        } else {
            backend.decoderCandidates.insert(backend.decoderCandidates.begin(), requested);
        }
    }

    auto selectedDecoderName = chooseFactory(backend.decoderCandidates);
    auto selectedSinkName = chooseFactory(backend.sinkCandidates);
#ifdef PADMIRROR_TEST_HOOKS
    if (const auto* overrideDecoder = std::getenv("PADMIRROR_TEST_VIDEO_DECODER");
        overrideDecoder && *overrideDecoder) {
        if (auto* factory = gst_element_factory_find(overrideDecoder)) {
            gst_object_unref(factory);
            selectedDecoderName = overrideDecoder;
        }
    }
    if (const auto* overrideSink = std::getenv("PADMIRROR_TEST_VIDEO_SINK");
        overrideSink && *overrideSink) {
        if (auto* factory = gst_element_factory_find(overrideSink)) {
            gst_object_unref(factory);
            selectedSinkName = overrideSink;
        }
    }
#endif
    if (selectedDecoderName.empty() || selectedSinkName.empty()) {
        if (errorHandler) {
            errorHandler(selectedDecoderName.empty()
                ? (codec == core::VideoCodec::Hevc
                    ? "HEVC decoder is unavailable"
                    : "D3D11 H.264 decoder is unavailable")
                : "D3D11 video sink is unavailable");
        }
        return false;
    }

    auto* pipeline = gst_pipeline_new("padmirror-video");
    auto* source = gst_element_factory_make("appsrc", "video-source");
    auto* queue = gst_element_factory_make("queue", "live-video-queue");
    const auto* parserFactory = codec == core::VideoCodec::Hevc ? "h265parse" : "h264parse";
    auto* parser = gst_element_factory_make(parserFactory, "video-parser");
    auto* decoder = gst_element_factory_make(selectedDecoderName.c_str(), "hardware-video-decoder");
    // Colour-space converter between decoder and sink. AirPlay screen mirroring
    // delivers BT.709 LIMITED range (luma 16-235); if the sink presents that as
    // full range (0-255) the black level lifts and the whole picture looks
    // washed out / ~30% too bright with slight banding -- exactly the reported
    // mismatch vs. the iPad. d3d11convert runs on the same D3D11 device as the
    // decoder and sink (zero extra CPU copies) and performs the correct
    // limited->full expansion during negotiation. Optional: if the plugin is
    // unavailable we link without it rather than fail the pipeline.
    auto* convert = gst_element_factory_make("d3d11convert", "video-color-convert");
    auto* sink = gst_element_factory_make(selectedSinkName.c_str(), "video-output");
    if (!pipeline || !source || !queue || !parser || !decoder || !sink) {
        if (pipeline) gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot create the GStreamer video pipeline");
        return false;
    }

    auto* caps = gst_caps_new_simple(
        codec == core::VideoCodec::Hevc ? "video/x-h265" : "video/x-h264",
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
    // Never drop ENCODED video frames. H.264/HEVC are reference-coded: dropping
    // any access unit (the IDR or a P-frame) breaks the decode chain and yields
    // persistent macroblock corruption until the next keyframe -- exactly the
    // "vo hinh" seen on connect, when the iPad bursts SPS/PPS+IDR+frames faster
    // than a 3-deep leaky source could hold. Keep the source lossless
    // (unbounded, non-leaky); latency is bounded downstream on DECODED frames,
    // which are independent and safe to drop.
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "max-buffers")) {
        g_object_set(source, "max-buffers", static_cast<guint64>(0), nullptr);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "leaky-type")) {
        g_object_set(source, "leaky-type", 0, nullptr);
    }

    g_object_set(
        queue,
        "max-size-buffers", 3,
        "max-size-bytes", 0,
        "max-size-time", static_cast<guint64>(0),
        "leaky", 2,
        nullptr);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(parser), "config-interval")) {
        g_object_set(parser, "config-interval", -1, nullptr);
    }
    setBooleanIfPresent(parser, "disable-passthrough", TRUE);
    setBooleanIfPresent(decoder, "discard-corrupted-frames", FALSE);
    // Motion-to-photon latency: iPad screen mirroring is encoded WITHOUT
    // B-frames, so the decoder never needs to hold frames to reorder them.
    // Left at their defaults, the D3D11 decoders keep a DPB reorder window
    // (max-display-delay frames) before emitting, which shows up as the iPad
    // moving and PadMirror following a few frames later. Force zero reorder
    // delay / low-latency output so each decoded frame is presented as soon
    // as it is ready. Both no-op if the selected decoder lacks the property.
    setBooleanIfPresent(decoder, "low-latency", TRUE);
    setIntIfPresent(decoder, "max-display-delay", 0);
    setBooleanIfPresent(sink, "sync", TRUE);
    setBooleanIfPresent(sink, "async", FALSE);
    setBooleanIfPresent(sink, "qos", FALSE);
    setBooleanIfPresent(sink, "enable-last-sample", TRUE);
    setBooleanIfPresent(sink, "force-aspect-ratio", settings.maintainAspectRatio());
    setInt64IfPresent(sink, "max-lateness", -1);
    setUnsigned64IfPresent(sink, "processing-deadline", 0);

    // Queue sits on the DECODED side (decoder -> queue -> sink). leaky=downstream
    // there drops only late decoded frames (a skipped display frame, harmless),
    // never encoded reference frames -- honouring the documented "max 3, leaky
    // downstream" latency bound while keeping the encoded path lossless.
    if (convert) {
        gst_bin_add_many(GST_BIN(pipeline), source, parser, decoder, convert, queue, sink, nullptr);
        if (!gst_element_link_many(source, parser, decoder, convert, queue, sink, nullptr)) {
            gst_object_unref(pipeline);
            if (errorHandler) errorHandler("Cannot link the bounded D3D11 video pipeline");
            return false;
        }
    } else {
        gst_bin_add_many(GST_BIN(pipeline), source, parser, decoder, queue, sink, nullptr);
        if (!gst_element_link_many(source, parser, decoder, queue, sink, nullptr)) {
            gst_object_unref(pipeline);
            if (errorHandler) errorHandler("Cannot link the bounded D3D11 video pipeline");
            return false;
        }
    }

    if (auto* pad = gst_element_get_static_pad(decoder, "src")) {
        gst_pad_add_probe(
            pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            metricsProbe,
            new ProbeContext{
                metrics,
                true,
                codec == core::VideoCodec::Hevc ? QStringLiteral("HEVC") : QStringLiteral("H.264")},
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

    {
        std::lock_guard lock(mutex_);
        pipeline_ = pipeline;
        appSource_ = source;
        sink_ = sink;
        metrics_ = metrics;
        errorHandler_ = std::move(errorHandler);
        windowHandle_ = windowHandle;
        codec_ = codec;
        sourcePtsAnchor_ = 0;
        runningPtsAnchor_ = 0;
        lastVideoPts_ = 0;
        videoTimelineInitialized_ = false;
        if (windowHandle_ != 0 && GST_IS_VIDEO_OVERLAY(sink_)) {
            gst_video_overlay_set_window_handle(
                GST_VIDEO_OVERLAY(sink_), static_cast<guintptr>(windowHandle_));
        }
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
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
    videoTimelineInitialized_ = false;
}

bool VideoPipeline::push(core::EncodedVideoPacket packet) {
    if (!packet.payload || packet.payload->empty()) return false;

    std::lock_guard lock(mutex_);
    if (!appSource_) return false;
    if (packet.codec != codec_) {
        if (errorHandler_) errorHandler_("The video packet codec does not match the active pipeline");
        return false;
    }

    const auto now = runningTime(pipeline_);
    std::uint64_t pts = now;
    constexpr auto timelineTolerance = 120 * GST_MSECOND;
    if (!videoTimelineInitialized_) {
        sourcePtsAnchor_ = packet.ptsNs;
        runningPtsAnchor_ = now + initialLeadNs_;
        pts = runningPtsAnchor_;
        videoTimelineInitialized_ = true;
    } else if (packet.ptsNs != 0 && sourcePtsAnchor_ != 0 && packet.ptsNs >= sourcePtsAnchor_) {
        const auto candidate = runningPtsAnchor_ + (packet.ptsNs - sourcePtsAnchor_);
        const bool stale = candidate + timelineTolerance < now;
        const bool tooFarAhead = candidate > now + timelineTolerance;
        if (stale || tooFarAhead) {
            sourcePtsAnchor_ = packet.ptsNs;
            runningPtsAnchor_ = now + initialLeadNs_;
            pts = runningPtsAnchor_;
        } else {
            pts = candidate;
        }
    }
    if (lastVideoPts_ != 0 && pts <= lastVideoPts_) pts = lastVideoPts_ + 1;
    lastVideoPts_ = pts;

    auto* buffer = gst_buffer_new_allocate(nullptr, packet.payload->size(), nullptr);
    if (!buffer) return false;
    gst_buffer_fill(buffer, 0, packet.payload->data(), packet.payload->size());
    GST_BUFFER_PTS(buffer) = pts;
    GST_BUFFER_DTS(buffer) = pts;
    GST_BUFFER_DURATION(buffer) = GST_SECOND / 60;
    if (!packet.keyframe) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    if (packet.discontinuity) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
    if (metrics_) metrics_->sourceFrame();
    return gst_app_src_push_buffer(GST_APP_SRC(appSource_), buffer) == GST_FLOW_OK;
}

void VideoPipeline::flush() {
    std::lock_guard lock(mutex_);
    if (!pipeline_) return;
    gst_element_send_event(pipeline_, gst_event_new_flush_start());
    gst_element_send_event(pipeline_, gst_event_new_flush_stop(TRUE));
    sourcePtsAnchor_ = 0;
    runningPtsAnchor_ = 0;
    lastVideoPts_ = 0;
    videoTimelineInitialized_ = false;
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
