#include "media/AudioPipeline.h"

#include "app/Settings.h"
#include "diagnostics/Metrics.h"
#include "platform/PlatformAudio.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <QVariantMap>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
#ifdef PADMIRROR_TEST_HOOKS
    if (const auto* overrideName = std::getenv("PADMIRROR_TEST_AUDIO_SINK");
        overrideName && *overrideName) {
        if (auto* factory = gst_element_factory_find(overrideName)) {
            gst_object_unref(factory);
            actualName = QString::fromUtf8(overrideName);
            return gst_element_factory_make(overrideName, "audio-output");
        }
    }
#endif
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

void setInt64IfPresent(GstElement* element, const char* property, gint64 value) {
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

bool waitForAudioLead(
    GstElement* pipeline,
    std::uint64_t pts,
    std::uint64_t leadNs,
    const std::atomic_bool& stopRequested) {
    constexpr auto safetyNs = GST_MSECOND;
    while (!stopRequested.load()) {
        const auto now = runningTime(pipeline);
        if (now == 0 || pts <= now + leadNs + safetyNs) return true;
        const auto remaining = pts - (now + leadNs);
        std::this_thread::sleep_for(std::chrono::nanoseconds(
            std::min<std::uint64_t>(remaining, 2 * GST_MSECOND)));
    }
    return false;
}

} // namespace

AudioPipeline::~AudioPipeline() {
    stop();
}

bool AudioPipeline::start(
    const app::Settings& settings,
    core::AudioCodec codec,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();

    // AAC-ELD (AirPlay) uses a stable ~90 ms sink buffer, so its presentation
    // timeline needs a matching lead: with sync=TRUE a 10 ms lead would schedule
    // samples almost at the live edge and the sink would drop them as late,
    // which itself crackles. Give the encoded path enough runway to fill the
    // sink cleanly; the PCM (USB) path keeps its original tight lead.
    const std::uint64_t baseLeadMs = static_cast<std::uint64_t>(std::max(settings.audioBufferMs(), 5));
    initialLeadNs_ = (codec == core::AudioCodec::AacEld ? std::uint64_t{80} : baseLeadMs) * GST_MSECOND;

    // AAC-ELD is decoded to PCM in-process (avdec_aac cannot decode AOT-39).
    // Bring up the libfdk-aac decoder before the pipeline starts; if the
    // library is unavailable, fail cleanly with a clear message rather than
    // publishing a silent/garbled stream.
    std::unique_ptr<FdkAacEldDecoder> eldDecoder;
    if (codec == core::AudioCodec::AacEld) {
        // AudioSpecificConfig for AAC-ELD (AOT 39), 44.1 kHz (sfi=4), stereo,
        // frameLengthFlag=1 => 480 samples/frame: 0xF8 0xE8 0x50 0x00. This is
        // UxPlay's exact value (f8e85000). The frameLengthFlag MATTERS: AirPlay
        // screen audio sends 480-sample AAC-ELD frames, so configuring the
        // decoder for the default 512-sample frame (byte 0x40) makes it decode
        // every AU against the wrong frame length -> continuously harsh/screechy
        // PCM even though audio comes out. 0x50 sets the 480-sample layout.
        static constexpr std::uint8_t kAacEldAsc[] = {0xF8, 0xE8, 0x50, 0x00};
        eldDecoder = std::make_unique<FdkAacEldDecoder>();
        if (!eldDecoder->open(kAacEldAsc, sizeof(kAacEldAsc))) {
            const std::string reason = eldDecoder->error();
            if (errorHandler) {
                errorHandler("AAC-ELD audio decoder unavailable: " + reason);
            }
            return false;
        }
    }
    QString actualDevice;
    auto* sink = createSelectedAudioSink(settings.audioDevice(), actualDevice);
    auto* pipeline = gst_pipeline_new("padmirror-audio");
    auto* source = gst_element_factory_make("appsrc", "audio-source");
    // AAC-ELD (AirPlay) is decoded to PCM in-process by libfdk-aac (see
    // FdkAacEldDecoder / push()), NOT by a GStreamer element: avdec_aac cannot
    // decode AOT-39 and produces garbled or silent audio. The pipeline is
    // therefore raw-PCM end to end for both codecs -- no decoder element.
    GstElement* decoder = nullptr;
    auto* convert = gst_element_factory_make("audioconvert", "audio-convert");
    auto* resample = gst_element_factory_make("audioresample", "audio-resample");
    auto* capsFilter = gst_element_factory_make("capsfilter", "audio-48khz");
    auto* queue = gst_element_factory_make("queue", "live-audio-queue");
    if (!pipeline || !source || !convert || !resample || !capsFilter || !queue || !sink) {
        if (pipeline) gst_object_unref(pipeline);
        if (errorHandler) errorHandler("Cannot create the GStreamer audio pipeline");
        return false;
    }

    GstCaps* inputCaps = nullptr;
    if (codec == core::AudioCodec::AacEld) {
        // libfdk-aac decodes AAC-ELD to interleaved 16-bit PCM; audioresample
        // below converts it to the 48 kHz WASAPI output rate. Use the rate and
        // channel count the decoder ACTUALLY reports for this stream (44.1 kHz
        // stereo for AirPlay screen audio) rather than assuming: a caps/PCM
        // mismatch replays the samples at the wrong rate and sounds harsh.
        const int eldRate = eldDecoder->sampleRate() > 0 ? eldDecoder->sampleRate() : 44100;
        const int eldChannels = eldDecoder->channels() > 0 ? eldDecoder->channels() : 2;
        inputCaps = gst_caps_new_simple(
            "audio/x-raw",
            "format", G_TYPE_STRING, "S16LE",
            "layout", G_TYPE_STRING, "interleaved",
            "rate", G_TYPE_INT, eldRate,
            "channels", G_TYPE_INT, eldChannels,
            nullptr);
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
    const bool pcm = codec == core::AudioCodec::PcmS16Le;
    const bool aacEld = codec == core::AudioCodec::AacEld;
    const bool safeMode = settings.audioMode() == app::Settings::AudioMode::Safe;
    const bool exclusive = settings.audioMode() == app::Settings::AudioMode::Exclusive;
    const int targetBufferMs = safeMode ? std::max(settings.audioBufferMs(), 20)
                                       : std::max(settings.audioBufferMs(), 5);
    constexpr auto bytesPerPcmMillisecond = 48000U * 2U * 2U / 1000U;
    g_object_set(
        source,
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "block", FALSE,
        "max-bytes", static_cast<guint64>(aacEld ? 0 : bytesPerPcmMillisecond * 35),
        "caps", inputCaps,
        nullptr);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "max-buffers")) {
        // AAC-ELD now carries DECODED PCM (the libfdk-aac decode happens before
        // the appsrc), so there is no downstream decoder left to starve -- the
        // old "lossless AAC source" rule no longer applies. Bound the source to
        // ~300 ms and let it drop the OLDEST buffer when a post-seek / resume
        // burst overflows it, so buffered audio can never accumulate into a
        // multi-second delay. push() already trims bursts at the live edge; this
        // is a hard safety cap behind that. PCM (USB) path keeps its own tuning.
        g_object_set(source, "max-buffers",
            pcm ? static_cast<guint64>(0)
                : (aacEld ? static_cast<guint64>(30) : static_cast<guint64>(16)),
            nullptr);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "leaky-type")) {
        // 0 = no leak (PCM), 2 = leak downstream / drop oldest (AAC-ELD + other).
        g_object_set(source, "leaky-type", pcm ? 0 : 2, nullptr);
    }
    g_object_set(capsFilter, "caps", outputCaps, nullptr);
    gst_caps_unref(inputCaps);
    gst_caps_unref(outputCaps);

    // Encoded AAC-ELD (AirPlay) audio must NOT be leaky-dropped: unlike a skipped
    // decoded video frame, dropping PCM samples to "catch up" is always audible
    // as clicks/crackle ("re/choi"). Give it a real jitter buffer and let the
    // sync=TRUE sink handle timing. The PCM (USB) path keeps the tight 35 ms
    // drop-oldest live cap it was designed around.
    g_object_set(
        queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", static_cast<guint64>(aacEld ? 200 : 35) * GST_MSECOND,
        "leaky", aacEld ? 0 : 2,
        nullptr);
    setBooleanIfPresent(sink, "sync", TRUE);
    setBooleanIfPresent(sink, "async", FALSE);
    setBooleanIfPresent(sink, "qos", FALSE);
    setBooleanIfPresent(sink, "provide-clock", TRUE);
    if (aacEld) {
        // AirPlay AAC-ELD: a shared-mode WASAPI buffer of ~10 ms (the PCM/USB
        // low-latency tuning) underruns on the slightest scheduling jitter, and
        // every underrun is an audible click -- the reported screech. Use a
        // stable buffer WASAPI can actually service (~90 ms), shared mode, and
        // do NOT force ultra-low latency-time. This adds a little latency but
        // makes screen-mirror audio clean and stable, which is what was asked.
        setBooleanIfPresent(sink, "low-latency", FALSE);
        setBooleanIfPresent(sink, "exclusive", FALSE);
        setInt64IfPresent(sink, "buffer-time", static_cast<gint64>(90000));
        setInt64IfPresent(sink, "latency-time", static_cast<gint64>(20000));
        setUnsigned64IfPresent(sink, "alignment-threshold", 40 * GST_MSECOND);
        setUnsigned64IfPresent(sink, "discont-wait", 100 * GST_MSECOND);
    } else {
        setBooleanIfPresent(sink, "low-latency", safeMode ? FALSE : TRUE);
        setBooleanIfPresent(sink, "exclusive", exclusive ? TRUE : FALSE);
        setInt64IfPresent(sink, "buffer-time", static_cast<gint64>(std::min(targetBufferMs, 35) * 1000));
        setInt64IfPresent(sink, "latency-time", safeMode ? 10000 : (targetBufferMs <= 5 ? 2500 : 5000));
        setInt64IfPresent(sink, "drift-tolerance", 40000);
        setUnsigned64IfPresent(sink, "alignment-threshold", 20 * GST_MSECOND);
        setUnsigned64IfPresent(sink, "discont-wait", 100 * GST_MSECOND);
    }
    setUnsigned64IfPresent(sink, "processing-deadline", 0);

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

    std::shared_ptr<AudioRingBuffer> ringBuffer;
    if (codec == core::AudioCodec::PcmS16Le) {
        ringBuffer = std::make_shared<AudioRingBuffer>(
            48000, 2, 2, static_cast<double>(std::min(targetBufferMs, 35)), 35.0);
    }
    {
        std::lock_guard lock(mutex_);
        ringBuffer_ = std::move(ringBuffer);
        pipeline_ = pipeline;
        appSource_ = source;
        metrics_ = metrics;
        errorHandler_ = std::move(errorHandler);
        nextPtsNs_ = 0;
        sourcePtsAnchor_ = 0;
        runningPtsAnchor_ = 0;
        inputTimelineInitialized_ = false;
        ptsInitialized_ = false;
        resyncRequested_.store(true);
        unsupportedAudioReported_.store(false);
        codec_ = codec;
        eldDecoder_ = std::move(eldDecoder);
        eldPcm_.clear();
    }
    if (metrics) metrics->setAudioBackend(actualDevice);

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stop();
        return false;
    }
    stopRequested_.store(false);
    if (codec == core::AudioCodec::PcmS16Le) {
        feederThread_ = std::thread([this] { feed(); });
    }
    busThread_ = std::thread([this] { monitorBus(); });
    return true;
}

void AudioPipeline::stop() {
    stopRequested_.store(true);
    std::shared_ptr<AudioRingBuffer> ringBuffer;
    {
        std::lock_guard lock(mutex_);
        ringBuffer = ringBuffer_;
        if (appSource_) gst_app_src_end_of_stream(GST_APP_SRC(appSource_));
        if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
    if (ringBuffer) ringBuffer->wakeAll();
    if (feederThread_.joinable()) feederThread_.join();
    if (busThread_.joinable()) busThread_.join();

    std::lock_guard lock(mutex_);
    if (pipeline_) gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    appSource_ = nullptr;
    metrics_ = nullptr;
    ringBuffer_.reset();
    eldDecoder_.reset();
    eldPcm_.clear();
    sourcePtsAnchor_ = 0;
    runningPtsAnchor_ = 0;
    ptsInitialized_ = false;
    inputTimelineInitialized_ = false;
}

bool AudioPipeline::push(core::EncodedAudioPacket packet) {
    if (!packet.payload || packet.payload->empty()) return false;

    ErrorHandler unsupportedHandler;
    {
        std::lock_guard lock(mutex_);
        if (packet.codec != codec_) {
            bool expected = false;
            if (unsupportedAudioReported_.compare_exchange_strong(expected, true)) {
                unsupportedHandler = errorHandler_;
            }
        } else {
            if (codec_ == core::AudioCodec::PcmS16Le && packet.ptsNs != 0 && pipeline_) {
                const auto now = runningTime(pipeline_);
                if (!inputTimelineInitialized_) {
                    sourcePtsAnchor_ = packet.ptsNs;
                    runningPtsAnchor_ = now + initialLeadNs_;
                    inputTimelineInitialized_ = true;
                    if (!ptsInitialized_) {
                        nextPtsNs_ = runningPtsAnchor_;
                        ptsInitialized_ = true;
                    }
                } else if (packet.ptsNs >= sourcePtsAnchor_) {
                    const auto expected = runningPtsAnchor_ + (packet.ptsNs - sourcePtsAnchor_);
                    const auto target = now + initialLeadNs_;
                    const auto skew = expected >= target ? expected - target : target - expected;
                    if (skew > 120 * GST_MSECOND) {
                        sourcePtsAnchor_ = packet.ptsNs;
                        runningPtsAnchor_ = target;
                        nextPtsNs_ = target;
                        ptsInitialized_ = true;
                        resyncRequested_.store(true);
                    }
                }
            }

            if (codec_ == core::AudioCodec::AacEld) {
                if (!appSource_) return false;
                if (!eldDecoder_ || !eldDecoder_->valid()) return false;

                // Decode the AAC-ELD access unit to interleaved S16 PCM in
                // process (avdec_aac cannot decode AOT-39). Decode FIRST so the
                // buffer's PTS/duration are derived from the amount of audio the
                // decoder actually produced, not a hardcoded frame length: an
                // AAC-ELD screen frame is 480 samples/ch (10.884 ms @ 44.1 kHz),
                // but deriving the cadence from real output keeps the gapless
                // timeline sample-accurate even if the format differs, which is
                // what stops the harsh/crackly overlap artefacts.
                eldPcm_.clear();
                int decodedRate = 0;
                int decodedChannels = 0;
                if (!eldDecoder_->decode(
                        packet.payload->data(), packet.payload->size(),
                        eldPcm_, decodedRate, decodedChannels) ||
                    eldPcm_.empty() || decodedRate <= 0 || decodedChannels <= 0) {
                    // A single undecodable frame is not fatal; drop it without
                    // advancing the timeline so the next good frame resumes
                    // cleanly at the live edge.
                    return true;
                }

                const std::size_t samplesPerChannel = eldPcm_.size() / static_cast<std::size_t>(decodedChannels);
                const auto frameDurationNs = static_cast<GstClockTime>(
                    static_cast<std::uint64_t>(samplesPerChannel) * GST_SECOND /
                    static_cast<std::uint64_t>(decodedRate));

                // ---- UxPlay audio timing model (faithful port) ----
                //
                // UxPlay sets each audio buffer's PTS to the wall-clock time of the
                // audio itself: pts = ntp_time_local - pipeline_base_time, using a
                // GST_CLOCK_TYPE_REALTIME clock shared with video. It does NOT
                // re-lay a gapless timeline, run a burst governor, or flush on a
                // timestamp jump. Real gaps (switching a YouTube video, opening an
                // app, a skip, a quiet stretch in a game) are simply carried in the
                // timestamp, so audio always presents at the correct instant and
                // resumes with no dead air.
                //
                // PadMirror's two GStreamer pipelines use default clocks that start
                // near 0, and packet.ptsNs from AirPlayClockSynchronizer is still in
                // the NTP domain. So we do the one unavoidable conversion UxPlay
                // gets for free from its shared realtime clock: anchor the NTP
                // source time to pipeline running-time ONCE, then map every later
                // frame as anchorRunning + (sourcePts - anchorSource). This is the
                // identical mapping VideoPipeline already uses, which keeps audio
                // and video on a common timeline. After the anchor there is no
                // governor and no re-laying: the source clock and the pipeline clock
                // both advance in real time, so the mapped PTS stays at the live
                // edge on its own, exactly as in UxPlay.
                const std::uint64_t sourcePts = packet.ptsNs;
                const auto nowRunning = runningTime(pipeline_);

                if (packet.discontinuity || !inputTimelineInitialized_) {
                    // (Re)establish the single anchor. A discontinuity here means a
                    // genuine stream reset (format change / restart) that the
                    // coordinator already flushed; align the next audio to the live
                    // edge. AirPlayClockSynchronizer sets rebased only on real jumps.
                    sourcePtsAnchor_ = sourcePts;
                    runningPtsAnchor_ = nowRunning + initialLeadNs_;
                    inputTimelineInitialized_ = true;
                    ptsInitialized_ = true;
                    nextPtsNs_ = runningPtsAnchor_;
                }

                std::uint64_t pts;
                if (sourcePts >= sourcePtsAnchor_) {
                    pts = runningPtsAnchor_ + (sourcePts - sourcePtsAnchor_);
                } else {
                    // Source time ran backwards without a flagged discontinuity
                    // (out-of-order arrival the protocol buffer did not reorder):
                    // keep the timeline monotonic rather than snapping the anchor.
                    pts = nextPtsNs_;
                }
                // Buffers must be strictly increasing for the sink; nudge past the
                // previous PTS on the rare tie/regression instead of laying a fake
                // gapless cadence over the real timestamps.
                if (nextPtsNs_ != 0 && pts <= nextPtsNs_) {
                    pts = nextPtsNs_ + 1;
                }
                nextPtsNs_ = pts + frameDurationNs;

                const gsize pcmBytes = eldPcm_.size() * sizeof(std::int16_t);
                auto* buffer = gst_buffer_new_allocate(nullptr, pcmBytes, nullptr);
                if (!buffer) return false;
                gst_buffer_fill(buffer, 0, eldPcm_.data(), pcmBytes);
                GST_BUFFER_PTS(buffer) = pts;
                GST_BUFFER_DTS(buffer) = pts;
                GST_BUFFER_DURATION(buffer) = frameDurationNs;
                if (packet.discontinuity) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
                if (metrics_) metrics_->audioPacket(pts);
                const bool pushOk =
                    gst_app_src_push_buffer(GST_APP_SRC(appSource_), buffer) == GST_FLOW_OK;
                return pushOk;
            }

            const auto ringBuffer = ringBuffer_;
            if (!ringBuffer) return false;
            constexpr std::size_t bytesPerFrame = 4;
            const auto alignedBytes = packet.payload->size() - (packet.payload->size() % bytesPerFrame);
            if (alignedBytes == 0) return false;
            const auto result = ringBuffer->push(std::span<const std::uint8_t>(
                packet.payload->data(), alignedBytes));
            if (result.droppedBytes != 0) {
                resyncRequested_.store(true);
                if (metrics_) metrics_->audioResync();
            }
            if (metrics_) metrics_->setAudioBufferMs(ringBuffer->bufferedMilliseconds());
            return true;
        }
    }
    if (unsupportedHandler) {
        unsupportedHandler("The audio packet codec does not match the active pipeline");
    }
    return false;
}

void AudioPipeline::flush() {
    std::lock_guard lock(mutex_);
    flushLocked();
}

void AudioPipeline::flushLocked() {
    if (ringBuffer_) ringBuffer_->clear();
    if (pipeline_) {
        // flushLocked() now runs ONLY on a genuine stream reset (audio-format
        // change / restart / transport switch) -- MediaSession no longer calls
        // it for a mere timestamp jump, matching UxPlay, which flushes audio
        // only on an actual RTSP FLUSH or format change. reset_time=FALSE keeps
        // the pipeline clock intact so the sync=TRUE WASAPI sink does not
        // re-preroll from zero (that re-preroll was the ~900 ms of dead air);
        // the next buffer re-anchors to the live edge in push().
        gst_element_send_event(pipeline_, gst_event_new_flush_start());
        gst_element_send_event(pipeline_, gst_event_new_flush_stop(FALSE));
    }
    sourcePtsAnchor_ = 0;
    runningPtsAnchor_ = 0;
    nextPtsNs_ = 0;
    inputTimelineInitialized_ = false;
    ptsInitialized_ = false;
    resyncRequested_.store(true);
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
    constexpr std::size_t framesPerChunk = 240;
    std::vector<std::uint8_t> chunk(bytesPerFrame * framesPerChunk);
    bool primed = false;
    bool wasUnderrun = false;
    bool discontinuity = true;

    while (!stopRequested_.load()) {
        std::shared_ptr<AudioRingBuffer> ring;
        {
            std::lock_guard lock(mutex_);
            ring = ringBuffer_;
        }
        if (!ring) break;
        if (!primed) {
            if (!ring->waitForBytes(ring->targetBytes(), std::chrono::milliseconds(50))) continue;
            primed = true;
        }

        ring->waitForBytes(chunk.size(), std::chrono::milliseconds(20));
        auto popped = ring->pop(chunk);
        const bool hasAudio = popped != 0;
        if (!hasAudio) {
            popped = chunk.size();
            std::fill(chunk.begin(), chunk.end(), std::uint8_t{0});
            if (!wasUnderrun) {
                std::lock_guard lock(mutex_);
                if (metrics_) metrics_->audioUnderrun();
                wasUnderrun = true;
            }
        } else {
            if (wasUnderrun) resyncRequested_.store(true);
            wasUnderrun = false;
        }

        GstElement* source = nullptr;
        GstElement* pipeline = nullptr;
        std::uint64_t pts = 0;
        {
            std::lock_guard lock(mutex_);
            source = appSource_;
            pipeline = pipeline_;
            if (!source || !pipeline) break;
            if (!ptsInitialized_ || resyncRequested_.exchange(false)) {
                nextPtsNs_ = runningTime(pipeline) + initialLeadNs_;
                ptsInitialized_ = true;
                discontinuity = true;
            }
            pts = nextPtsNs_;
            nextPtsNs_ += (popped / bytesPerFrame) * GST_SECOND / 48000;
            if (metrics_) metrics_->setAudioBufferMs(ring->bufferedMilliseconds());
        }

        auto* buffer = gst_buffer_new_allocate(nullptr, popped, nullptr);
        gst_buffer_fill(buffer, 0, chunk.data(), popped);
        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DURATION(buffer) = (popped / bytesPerFrame) * GST_SECOND / 48000;
        if (metrics_) metrics_->audioPacket(pts);
        if (discontinuity) {
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
            discontinuity = false;
        }
        if (!hasAudio) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_GAP);
        if (!waitForAudioLead(pipeline, pts, initialLeadNs_, stopRequested_)) {
            gst_buffer_unref(buffer);
            break;
        }
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
            static_cast<GstMessageType>(
                GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
                GST_MESSAGE_WARNING | GST_MESSAGE_INFO));
        if (!message) continue;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            std::fprintf(stderr, "[audio-bus:ERROR] %s | %s\n",
                         error ? error->message : "(null)", debug ? debug : "");
            ErrorHandler handler;
            {
                std::lock_guard lock(mutex_);
                handler = errorHandler_;
            }
            if (handler) handler(error ? error->message : "GStreamer audio error");
            if (error) g_error_free(error);
            g_free(debug);
        } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_WARNING) {
            // avdec_aac emits WARNINGs (not errors) when it mis-parses a stream
            // it cannot truly decode -- e.g. AAC-ELD fed to the LC path. These
            // never reach the ERROR bus filter, so they were invisible before.
            // Surface them so the decode failure mode is diagnosable from logs.
            GError* warn = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(message, &warn, &debug);
            std::fprintf(stderr, "[audio-bus:WARN] %s | %s\n",
                         warn ? warn->message : "(null)", debug ? debug : "");
            if (warn) g_error_free(warn);
            g_free(debug);
        } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_INFO) {
            GError* info = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_info(message, &info, &debug);
            std::fprintf(stderr, "[audio-bus:INFO] %s | %s\n",
                         info ? info->message : "(null)", debug ? debug : "");
            if (info) g_error_free(info);
            g_free(debug);
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
}

} // namespace padmirror::media
