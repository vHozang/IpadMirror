/**
 * RPiPlay - An open-source AirPlay mirroring server for Raspberry Pi
 * Copyright (C) 2019 Florian Draschbacher
 * Modified for:
 * UxPlay - An open-source AirPlay mirroring server
 * Copyright (C) 2021-23 F. Duncanh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include <math.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "audio_renderer.h"
#include "fdk_aac_eld_decoder.h"
#define NFORMATS 2     /* set to 4 to enable AAC_LD and PCM:  allowed, but  never seen in real-world use */

static logger_t *logger = NULL;
const char * format[NFORMATS];

static const gchar *avdec_alac = "avdec_alac";
static gboolean aac_eld = FALSE;
static gboolean alac = FALSE;
static gboolean render_audio = FALSE;
static gboolean async = FALSE;
static gboolean vsync = FALSE;
static gboolean sync = FALSE;
static gboolean audio_rtp = FALSE;
static GstClockTime audio_remote_anchor_ns = GST_CLOCK_TIME_NONE;
static GstClockTime audio_local_anchor_ns = GST_CLOCK_TIME_NONE;
static GstClockTime audio_last_remote_ns = GST_CLOCK_TIME_NONE;
static gboolean audio_discontinuity = TRUE;
static gboolean audio_expected_seq_valid = FALSE;
static unsigned short audio_expected_seq = 0;

#define AUDIO_TIMESTAMP_JUMP_NS (250 * GST_MSECOND)
#define AUDIO_AAC_ELD_DURATION_NS ((480 * GST_SECOND) / 44100)
#define AUDIO_MAX_CONCEAL_FRAMES 8
#define AUDIO_APPSRC_MAX_TIME_NS (80 * GST_MSECOND)
#define AUDIO_QUEUE_MAX_TIME_NS (40 * GST_MSECOND)
#define AUDIO_MAX_PTS_LEAD_NS (150 * GST_MSECOND)
#define AUDIO_STATS_INTERVAL_NS (2 * GST_SECOND)

typedef struct audio_stats_state_s {
    GMutex mutex;
    GstClockTime window_start_ns;
    uint64_t appsrc_queued_ns;
    uint64_t queue_queued_ns;
    int64_t pts_lead_ns;
    uint64_t pushed_buffers;
    uint64_t appsrc_dropped;
    uint64_t queue_dropped;
    uint64_t appsrc_failures;
    uint64_t reanchors;
    uint64_t concealed_frames;
    uint64_t decoder_resets;
    uint64_t latency_recoveries;
    uint64_t generation;
} audio_stats_state_t;

typedef struct audio_renderer_s {
    GstElement *appsrc;
    GstElement *pipeline;
    GstElement *queue;
    GstElement *volume;
    GstBus *bus;
    unsigned char ct;
} audio_renderer_t ;
static audio_renderer_t *renderer_type[NFORMATS];
static audio_renderer_t *renderer = NULL;
static fdk_aac_eld_decoder_t *aac_eld_decoder = NULL;
static audio_stats_state_t audio_stats;
static gsize audio_stats_initialized = 0;

static void audio_stats_init(void)
{
    if (g_once_init_enter(&audio_stats_initialized)) {
        g_mutex_init(&audio_stats.mutex);
        g_once_init_leave(&audio_stats_initialized, 1);
    }
}

static void audio_stats_reset(void)
{
    audio_stats_init();
    g_mutex_lock(&audio_stats.mutex);
    const uint64_t generation = audio_stats.generation + 1;
    memset(&audio_stats.window_start_ns, 0,
           sizeof(audio_stats) - G_STRUCT_OFFSET(audio_stats_state_t, window_start_ns));
    audio_stats.window_start_ns = GST_CLOCK_TIME_NONE;
    audio_stats.generation = generation;
    g_mutex_unlock(&audio_stats.mutex);
}

static void audio_stats_update_levels_locked(GstClockTime pts)
{
    guint64 appsrc_time = 0;
    guint64 queue_time = 0;
    guint64 appsrc_dropped = 0;
    audio_stats.pts_lead_ns = 0;
    if (renderer && renderer->appsrc) {
        g_object_get(renderer->appsrc,
                     "current-level-time", &appsrc_time,
                     "dropped", &appsrc_dropped,
                     NULL);
    }
    if (renderer && renderer->queue) {
        g_object_get(renderer->queue, "current-level-time", &queue_time, NULL);
    }
    audio_stats.appsrc_queued_ns = appsrc_time;
    audio_stats.queue_queued_ns = queue_time;
    audio_stats.appsrc_dropped = appsrc_dropped;
    if (sync && renderer && renderer->pipeline && GST_CLOCK_TIME_IS_VALID(pts)) {
        const GstClockTime running_time =
            gst_element_get_current_running_time(renderer->pipeline);
        if (GST_CLOCK_TIME_IS_VALID(running_time)) {
            audio_stats.pts_lead_ns = GST_CLOCK_DIFF(running_time, pts);
        }
    }
}

static void audio_stats_log_window_locked(GstClockTime now, GstClockTime pts)
{
    audio_stats_update_levels_locked(pts);
    if (!GST_CLOCK_TIME_IS_VALID(audio_stats.window_start_ns)) {
        audio_stats.window_start_ns = now;
        return;
    }
    if (now - audio_stats.window_start_ns < AUDIO_STATS_INTERVAL_NS) {
        return;
    }
    logger_log(logger, LOGGER_INFO,
               "Audio latency appsrc=%.1f ms queue=%.1f ms pts-lead=%.1f ms drops=%llu/%llu failures=%llu recoveries=%llu concealed=%llu resets=%llu",
               (double)audio_stats.appsrc_queued_ns / GST_MSECOND,
               (double)audio_stats.queue_queued_ns / GST_MSECOND,
               (double)audio_stats.pts_lead_ns / GST_MSECOND,
               (unsigned long long)audio_stats.appsrc_dropped,
               (unsigned long long)audio_stats.queue_dropped,
               (unsigned long long)audio_stats.appsrc_failures,
               (unsigned long long)audio_stats.latency_recoveries,
               (unsigned long long)audio_stats.concealed_frames,
               (unsigned long long)audio_stats.decoder_resets);
    audio_stats.window_start_ns = now;
    audio_stats.generation++;
}

static void audio_queue_overrun(GstElement *queue, gpointer user_data)
{
    (void)queue;
    (void)user_data;
    audio_stats_init();
    g_mutex_lock(&audio_stats.mutex);
    audio_stats.queue_dropped++;
    g_mutex_unlock(&audio_stats.mutex);
}

/* GStreamer Caps strings for Airplay-defined audio compression types (ct) */

/* ct = 1; linear PCM (uncompressed): 44100/16/2, S16LE */
static const char lpcm_caps[]="audio/x-raw,rate=(int)44100,channels=(int)2,format=S16LE,layout=interleaved";

/* ct = 2; codec_data is ALAC magic cookie:  44100/16/2 spf = 352 */    
static const char alac_caps[] = "audio/x-alac,mpegversion=(int)4,channels=(int)2,rate=(int)44100,stream-format=raw,codec_data=(buffer)"
                           "00000024""616c6163""00000000""00000160""0010280a""0e0200ff""00000000""00000000""0000ac44";

/* ct = 4; codec_data from MPEG v4 ISO 14996-3 Section 1.6.2.1:  AAC-LC 44100/2 spf = 1024 */
static const char aac_lc_caps[] ="audio/mpeg,mpegversion=(int)4,channels=(int)2,rate=(int)44100,stream-format=raw,codec_data=(buffer)1210";

/* ct = 8; codec_data from MPEG v4 ISO 14996-3 Section 1.6.2.1: AAC_ELD 44100/2  spf = 480 */

static gboolean check_plugins (void)
{
    GstRegistry *registry = NULL;
    const gchar *needed[] = { "app", "libav", NULL};
    const gchar *gst[] = {"plugins-base", "libav", NULL};
    registry = gst_registry_get ();
    gboolean ret = TRUE;
    for (int i = 0; i < g_strv_length ((gchar **) needed); i++) {
        GstPlugin *plugin = NULL;
        plugin = gst_registry_find_plugin (registry, needed[i]);
        if (!plugin) {
            g_print ("Required gstreamer plugin '%s' not found\n"
                     "Missing plugin is contained in  '[GStreamer 1.x]-%s'\n",needed[i], gst[i]);
            ret = FALSE;
            continue;
        }
        gst_object_unref (plugin);
        plugin = NULL;
    }
    if (ret == FALSE) {
        g_print ("\nif the plugin is installed, but not found, your gstreamer registry may have been corrupted.\n"
                 "to rebuild it when gstreamer next starts, clear your gstreamer cache with:\n"
                 "\"rm -rf ~/.cache/gstreamer-1.0\"\n\n");
    }
    return ret;
}

static gboolean check_plugin_feature (const gchar *needed_feature)
{
    GstPluginFeature *plugin_feature = NULL;
    GstRegistry *registry = gst_registry_get ();
    gboolean ret = TRUE;

    plugin_feature = gst_registry_find_feature (registry, needed_feature, GST_TYPE_ELEMENT_FACTORY);
    if (!plugin_feature) {
        g_print ("Required gstreamer libav plugin feature '%s' not found:\n\n"
	         "This may be missing because the FFmpeg package used by GStreamer-1.x-libav is incomplete.\n"
	         "(Some distributions provide an incomplete FFmpeg due to License or Patent issues:\n"
	         "in such cases a complete version for that distribution is usually made available elsewhere)\n",
	         needed_feature);
        ret = FALSE;
    } else {
        gst_object_unref (plugin_feature);
        plugin_feature = NULL;
    }
    if (ret == FALSE) {
        g_print ("\nif the plugin feature is installed, but not found, your gstreamer registry may have been corrupted.\n"
                 "to rebuild it when gstreamer next starts, clear your gstreamer cache with:\n"
                 "\"rm -rf ~/.cache/gstreamer-1.0\"\n\n");
    }
    return ret;
}

bool gstreamer_init(){
    gst_init(NULL,NULL);    
    return (bool) check_plugins ();
}

void audio_renderer_init(logger_t *render_logger, const char* audiosink, const bool* audio_sync, const bool* video_sync, const char *artp_pipeline) {
    GError *error = NULL;
    GstCaps *caps = NULL;

    audio_stats_reset();
    audio_rtp = (bool) strlen(artp_pipeline);
    if (audio_rtp) {
        g_print("*** Audio RTP mode enabled: sending to %s\n", artp_pipeline);
    }

    logger = render_logger;
    
    char fdk_error[256] = {0};
    aac_eld_decoder = fdk_aac_eld_decoder_create(fdk_error, sizeof(fdk_error));
    aac_eld = (aac_eld_decoder != NULL);
    if (!aac_eld) {
        logger_log(logger, LOGGER_ERR, "AAC-ELD decoder unavailable: %s", fdk_error);
    }
    alac = check_plugin_feature (avdec_alac);

    for (int i = 0; i < NFORMATS ; i++) {
        renderer_type[i] = (audio_renderer_t *)  calloc(1,sizeof(audio_renderer_t));
        g_assert(renderer_type[i]);
        GString *launch = g_string_new("appsrc name=audio_source ! ");
        g_string_append_printf(
            launch,
            "queue name=audio_live_queue max-size-buffers=0 "
            "max-size-bytes=0 max-size-time=%llu leaky=downstream ! ",
            (unsigned long long)AUDIO_QUEUE_MAX_TIME_NS);
        switch (i) {
        case 0:    /* AAC-ELD is decoded by libfdk-aac before appsrc */
            break;
        case 1:    /* ALAC */
            if (alac) g_string_append(launch, "avdec_alac ! ");
            break;
        case 3:   /*PCM*/
            break;
        default:
            break;
        }
        /* Keep resampling in floating point so loud 44.1 kHz content does
         * not clip on conversion to the Windows endpoint sample rate. */
        g_string_append (launch, "audioconvert ! audio/x-raw,format=F32LE ! ");
        g_string_append (launch, "audioresample quality=6 ! ");
        g_string_append (launch, "volume name=volume ! ");

        if (!audio_rtp) {
            /* Normal path: local audio output */
            g_string_append (launch, "audioconvert ! level ! ");
            g_string_append (launch, audiosink);
            switch(i) {
            case 1:  /*ALAC*/
                if (*audio_sync) {
                    g_string_append (launch, " sync=true");
                    async = TRUE;
                } else {
                    g_string_append (launch, " sync=false");
                    async = FALSE;
                }
                break;
            default:
                if (*video_sync) {
                    g_string_append (launch, " sync=true");
                    vsync = TRUE;
                } else {
                    g_string_append (launch, " sync=false");
                    vsync = FALSE;
                }
                break;
            }
        } else {
            /* RTP path: send decoded PCM over RTP */
            /* rtpL16pay requires S16BE (big-endian) format */
            g_string_append (launch, "audioconvert ! audio/x-raw,format=S16BE,rate=44100,channels=2 ! ");
            g_string_append (launch, "rtpL16pay ");
            g_string_append (launch, artp_pipeline);
        }
        renderer_type[i]->pipeline  = gst_parse_launch(launch->str, &error);
	if (error) {
          g_error ("gst_parse_launch error (audio %d):\n %s\n", i+1, error->message);
          g_clear_error (&error);
        }

        g_assert (renderer_type[i]->pipeline);
        GstClock *clock = gst_system_clock_obtain();
        g_object_set(clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
        gst_pipeline_use_clock(GST_PIPELINE_CAST(renderer_type[i]->pipeline), clock);
        renderer_type[i]->bus = gst_element_get_bus(renderer_type[i]->pipeline);
        renderer_type[i]->appsrc = gst_bin_get_by_name (GST_BIN (renderer_type[i]->pipeline), "audio_source");
        renderer_type[i]->queue = gst_bin_get_by_name (GST_BIN (renderer_type[i]->pipeline), "audio_live_queue");
        renderer_type[i]->volume = gst_bin_get_by_name (GST_BIN (renderer_type[i]->pipeline), "volume");
        g_assert(renderer_type[i]->appsrc);
        g_assert(renderer_type[i]->queue);
        g_signal_connect(renderer_type[i]->queue, "overrun",
                         G_CALLBACK(audio_queue_overrun), renderer_type[i]);
        switch (i) {
        case 0:
            caps =  gst_caps_from_string(lpcm_caps);
            renderer_type[i]->ct = 8;
            format[i] = "AAC-ELD 44100/2";
            break;
        case 1:
            caps =  gst_caps_from_string(alac_caps);
            renderer_type[i]->ct = 2;
            format[i] = "ALAC 44100/16/2";
            break;
        case 3:
            caps =  gst_caps_from_string(lpcm_caps);
            renderer_type[i]->ct = 1;
            format[i] = "PCM 44100/16/2 S16LE";
            break;
        default:
            break;
        }
        logger_log(logger, LOGGER_DEBUG, "Audio format %d: %s",i+1,format[i]);
        logger_log(logger, LOGGER_DEBUG, "GStreamer audio pipeline %d: \"%s\"", i+1, launch->str);
        g_string_free(launch, TRUE);
        g_object_set(renderer_type[i]->appsrc,
                     "caps", caps,
                     "stream-type", 0,
                     "is-live", TRUE,
                     "format", GST_FORMAT_TIME,
                     "block", FALSE,
                     "max-bytes", (guint64)0,
                     "max-buffers", (guint64)0,
                     "max-time", (guint64)AUDIO_APPSRC_MAX_TIME_NS,
                     "leaky-type", GST_APP_LEAKY_TYPE_DOWNSTREAM,
                     NULL);
        gst_caps_unref(caps);
        g_object_unref(clock);
    }
}

void audio_renderer_stop() {
    if (renderer) {
        gst_app_src_end_of_stream(GST_APP_SRC(renderer->appsrc));
        gst_element_set_state (renderer->pipeline, GST_STATE_NULL);
        renderer = NULL;
    }
}

static void get_renderer_type(unsigned char *ct, int *id) {
    render_audio = FALSE;
    *id = -1;
    for (int i = 0; i < NFORMATS; i++) {
        if (renderer_type[i]->ct == *ct) {
	    *id = i;
            break;
        }
    }
    switch (*id) {
    case 0:
        if (aac_eld) {
            render_audio = TRUE;
        } else {
            logger_log(logger, LOGGER_ERR, "*** libfdk-aac AAC-ELD decoder is unavailable");
        }
        sync = vsync;
        break;
    case 1:
        if (alac) {
            render_audio = TRUE;
        } else {
            logger_log(logger, LOGGER_INFO, "*** GStreamer libav plugin feature avdec_alac is missing, cannot decode ALAC audio");
        }
        sync = async;
        break;
    case 3:
        render_audio = TRUE;
	sync = FALSE;
        break;
    default:
        break;
    }
}

void  audio_renderer_start(unsigned char *ct) {
    int id = -1;
    audio_remote_anchor_ns = GST_CLOCK_TIME_NONE;
    audio_local_anchor_ns = GST_CLOCK_TIME_NONE;
    audio_last_remote_ns = GST_CLOCK_TIME_NONE;
    audio_discontinuity = TRUE;
    audio_expected_seq_valid = FALSE;
    audio_expected_seq = 0;
    audio_stats_reset();
    if (*ct == 8 && aac_eld_decoder) {
        char error[256] = {0};
        if (!fdk_aac_eld_decoder_reset(aac_eld_decoder, error, sizeof(error))) {
            aac_eld = FALSE;
            logger_log(logger, LOGGER_ERR, "AAC-ELD decoder reset failed: %s", error);
        }
    }
    get_renderer_type(ct, &id);
    if (id >= 0 && renderer) {
        if(*ct != renderer->ct) {
            gst_app_src_end_of_stream(GST_APP_SRC(renderer->appsrc));
            gst_element_set_state (renderer->pipeline, GST_STATE_NULL);
            logger_log(logger, LOGGER_INFO, "changed audio connection, format %s", format[id]);
            renderer = renderer_type[id];
            gst_element_set_state (renderer->pipeline, GST_STATE_PLAYING);
        }
    } else if (id >= 0) {
        logger_log(logger, LOGGER_INFO, "start audio connection, format %s", format[id]);
        renderer = renderer_type[id];
        gst_element_set_state (renderer->pipeline, GST_STATE_PLAYING);
    } else {
        logger_log(logger, LOGGER_ERR, "unknown audio compression type ct = %d", *ct);
    }
}

static gboolean push_audio_buffer(const unsigned char *data,
                                  size_t data_len,
                                  GstClockTime pts,
                                  GstClockTime duration)
{
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, data_len, NULL);
    if (!buffer) {
        logger_log(logger, LOGGER_ERR, "Unable to allocate GStreamer audio buffer");
        return FALSE;
    }

    if (sync && GST_CLOCK_TIME_IS_VALID(pts)) {
        GST_BUFFER_PTS(buffer) = pts;
    }
    if (GST_CLOCK_TIME_IS_VALID(duration)) {
        GST_BUFFER_DURATION(buffer) = duration;
    }
    if (audio_discontinuity) {
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
        audio_discontinuity = FALSE;
    }
    gst_buffer_fill(buffer, 0, data, data_len);

    const GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(renderer->appsrc), buffer);
    const GstClockTime now = gst_util_get_timestamp();
    audio_stats_init();
    g_mutex_lock(&audio_stats.mutex);
    audio_stats.pushed_buffers++;
    if (flow != GST_FLOW_OK) {
        audio_stats.appsrc_failures++;
    }
    audio_stats_log_window_locked(now, pts);
    g_mutex_unlock(&audio_stats.mutex);
    if (flow != GST_FLOW_OK) {
        logger_log(logger, LOGGER_WARNING,
                   "GStreamer audio appsrc rejected buffer: %s",
                   gst_flow_get_name(flow));
        audio_discontinuity = TRUE;
        return FALSE;
    }
    return TRUE;
}

static gboolean push_concealed_aac_eld_frame(GstClockTime pts,
                                              GstClockTime duration)
{
    const int16_t *pcm = NULL;
    size_t pcm_samples = 0;
    int sample_rate = 0;
    int channels = 0;
    int frame_size = 0;
    char error[256] = {0};

    if (!fdk_aac_eld_decoder_conceal(aac_eld_decoder, &pcm, &pcm_samples,
                                     &sample_rate, &channels, &frame_size,
                                     error, sizeof(error))) {
        logger_log(logger, LOGGER_WARNING,
                   "AAC-ELD packet-loss concealment failed: %s", error);
        return FALSE;
    }
    if (sample_rate != 44100 || channels != 2 || frame_size != 480) {
        logger_log(logger, LOGGER_WARNING,
                   "Unexpected concealed AAC-ELD format %d Hz/%d channels/%d samples",
                   sample_rate, channels, frame_size);
        return FALSE;
    }
    return push_audio_buffer((const unsigned char *)pcm,
                             pcm_samples * sizeof(*pcm), pts, duration);
}

void audio_renderer_render_buffer(unsigned char* data, int *data_len, unsigned short *seqnum, uint64_t *ntp_time) {
    const int16_t *pcm = NULL;
    size_t pcm_samples = 0;
    int sample_rate = 0;
    int channels = 0;
    int frame_size = 0;
    char decode_error[256] = {0};
    const unsigned char *push_data = data;
    size_t push_len = 0;
    GstClockTime pts = GST_CLOCK_TIME_NONE;
    GstClockTime duration = GST_CLOCK_TIME_NONE;
    unsigned short sequence_gap = 0;
    gboolean reset_sequence = FALSE;
    bool valid = false;

    if (!render_audio || !renderer || !data || !data_len || *data_len <= 0 ||
        !seqnum || !ntp_time) return;

    push_len = (size_t)*data_len;
    const GstClockTime remote_pts = (GstClockTime)*ntp_time;
    if (renderer->ct == 8) {
        duration = AUDIO_AAC_ELD_DURATION_NS;
        if (audio_expected_seq_valid) {
            const short delta = (short)(*seqnum - audio_expected_seq);
            if (delta < 0) {
                logger_log(logger, LOGGER_DEBUG,
                           "Ignoring late AAC-ELD sequence %u; expected %u",
                           *seqnum, audio_expected_seq);
                return;
            }
            if (delta > 0) {
                sequence_gap = (unsigned short)delta;
                if (sequence_gap > AUDIO_MAX_CONCEAL_FRAMES) {
                    logger_log(logger, LOGGER_WARNING,
                               "AAC-ELD sequence jumped from %u to %u; resetting timeline",
                               audio_expected_seq, *seqnum);
                    sequence_gap = 0;
                    reset_sequence = TRUE;
                } else {
                    logger_log(logger, LOGGER_WARNING,
                               "AAC-ELD sequence gap: expected %u, received %u; concealing %u frame(s)",
                               audio_expected_seq, *seqnum, sequence_gap);
                }
            }
        }
        audio_expected_seq = (unsigned short)(*seqnum + 1);
        audio_expected_seq_valid = TRUE;
    }

    if (sync) {
        GstClockTime local_now = gst_element_get_current_running_time(renderer->pipeline);
        gboolean reset_anchor = reset_sequence ||
                                !GST_CLOCK_TIME_IS_VALID(audio_remote_anchor_ns) ||
                                !GST_CLOCK_TIME_IS_VALID(audio_local_anchor_ns);
        if (!reset_anchor && GST_CLOCK_TIME_IS_VALID(audio_last_remote_ns)) {
            const GstClockTime expected = audio_last_remote_ns +
                (GST_CLOCK_TIME_IS_VALID(duration) ? duration : 0);
            const GstClockTime delta = remote_pts > expected
                ? remote_pts - expected
                : expected - remote_pts;
            reset_anchor = remote_pts < audio_last_remote_ns ||
                           delta > AUDIO_TIMESTAMP_JUMP_NS;
        }
        if (reset_anchor) {
            if (!GST_CLOCK_TIME_IS_VALID(local_now)) {
                local_now = 0;
            }
            audio_remote_anchor_ns = remote_pts;
            audio_local_anchor_ns = local_now;
            audio_discontinuity = TRUE;
            audio_stats_init();
            g_mutex_lock(&audio_stats.mutex);
            audio_stats.reanchors++;
            g_mutex_unlock(&audio_stats.mutex);
            logger_log(logger, LOGGER_INFO,
                       "Re-anchored audio timeline at remote %.6f s, local %.6f s",
                       (double)audio_remote_anchor_ns / GST_SECOND,
                       (double)audio_local_anchor_ns / GST_SECOND);
        }
        pts = audio_local_anchor_ns +
            (remote_pts >= audio_remote_anchor_ns
                ? remote_pts - audio_remote_anchor_ns
                : 0);
        if (GST_CLOCK_TIME_IS_VALID(local_now) &&
            GST_CLOCK_DIFF(local_now, pts) > (GstClockTimeDiff)AUDIO_MAX_PTS_LEAD_NS) {
            logger_log(logger, LOGGER_WARNING,
                       "Audio timeline led local playback by %.1f ms; recovering live edge",
                       (double)GST_CLOCK_DIFF(local_now, pts) / GST_MSECOND);
            audio_remote_anchor_ns = remote_pts;
            audio_local_anchor_ns = local_now;
            audio_last_remote_ns = GST_CLOCK_TIME_NONE;
            pts = local_now;
            audio_discontinuity = TRUE;
            audio_stats_init();
            g_mutex_lock(&audio_stats.mutex);
            audio_stats.reanchors++;
            audio_stats.latency_recoveries++;
            g_mutex_unlock(&audio_stats.mutex);
        }
    }

    if (renderer->ct == 8 && reset_sequence) {
        char reset_error[256] = {0};
        if (!fdk_aac_eld_decoder_reset(
                aac_eld_decoder, reset_error, sizeof(reset_error))) {
            logger_log(logger, LOGGER_ERR,
                       "AAC-ELD decoder gap reset failed: %s", reset_error);
            audio_discontinuity = TRUE;
            return;
        }
        audio_discontinuity = TRUE;
        audio_stats_init();
        g_mutex_lock(&audio_stats.mutex);
        audio_stats.decoder_resets++;
        g_mutex_unlock(&audio_stats.mutex);
    } else if (renderer->ct == 8 && sequence_gap &&
               GST_CLOCK_TIME_IS_VALID(duration)) {
        const GstClockTime concealed_span = (GstClockTime)sequence_gap * duration;
        const GstClockTime first_concealed_pts =
            GST_CLOCK_TIME_IS_VALID(pts) && pts > concealed_span
                ? pts - concealed_span
                : 0;
        audio_stats_init();
        g_mutex_lock(&audio_stats.mutex);
        audio_stats.concealed_frames += sequence_gap;
        g_mutex_unlock(&audio_stats.mutex);
        for (unsigned short missing = 0; missing < sequence_gap; ++missing) {
            if (!push_concealed_aac_eld_frame(
                    first_concealed_pts + (GstClockTime)missing * duration,
                    duration)) {
                audio_discontinuity = TRUE;
                break;
            }
        }
    }

    switch (renderer->ct) {
    case 8:
        switch (data[0]) {
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x80:
        case 0x81:
        case 0x82:
            valid = true;
            break;
        default:
            break;
        }
        if (valid) {
            valid = fdk_aac_eld_decoder_decode(aac_eld_decoder, data, (size_t)*data_len,
                                               &pcm, &pcm_samples, &sample_rate, &channels,
                                               &frame_size, decode_error, sizeof(decode_error));
            if (valid && (sample_rate != 44100 || channels != 2 || frame_size != 480)) {
                logger_log(logger, LOGGER_ERR,
                           "*** unexpected AAC-ELD output format %d Hz/%d channels/%d samples",
                           sample_rate, channels, frame_size);
                valid = false;
            }
            if (valid) {
                push_data = (const unsigned char *)pcm;
                push_len = pcm_samples * sizeof(*pcm);
            } else {
                logger_log(logger, LOGGER_ERR, "*** AAC-ELD decode failed: %s", decode_error);
            }
        }
        break;
    case 2:
        valid = (data[0] == 0x20);
        break;
    case 4:
        valid = (data[0] == 0xff);
        break;
    default:
        valid = true;
        break;
    }

    if (!valid) {
        logger_log(logger, LOGGER_ERR,
                   "*** invalid audio frame (compression_type %d) skipped; first byte 0x%2.2x",
                   renderer->ct, (unsigned int)data[0]);
        audio_discontinuity = TRUE;
        return;
    }

    if (push_audio_buffer(push_data, push_len, pts, duration) && sync) {
        audio_last_remote_ns = remote_pts;
    }
}

void audio_renderer_set_volume(double volume) {
    if (!renderer) {
       return;
    }
    volume = (volume > 1.0) ? 1.0 : volume;
    volume = (volume < 0.0) ? 0.0 : volume;
    g_object_set(renderer->volume, "volume", volume, NULL);
}

void audio_renderer_flush() {
    if (renderer && renderer->pipeline) {
        gst_element_send_event(renderer->pipeline, gst_event_new_flush_start());
        gst_element_send_event(renderer->pipeline, gst_event_new_flush_stop(FALSE));
    }
    if (aac_eld_decoder) {
        char error[256] = {0};
        if (!fdk_aac_eld_decoder_reset(aac_eld_decoder, error, sizeof(error))) {
            logger_log(logger, LOGGER_ERR, "AAC-ELD decoder flush reset failed: %s", error);
        }
    }
    audio_remote_anchor_ns = GST_CLOCK_TIME_NONE;
    audio_local_anchor_ns = GST_CLOCK_TIME_NONE;
    audio_last_remote_ns = GST_CLOCK_TIME_NONE;
    audio_discontinuity = TRUE;
    audio_expected_seq_valid = FALSE;
    audio_expected_seq = 0;
    audio_stats_reset();
}

bool audio_renderer_get_stats(audio_renderer_stats_t *stats)
{
    if (!stats) {
        return false;
    }
    audio_stats_init();
    g_mutex_lock(&audio_stats.mutex);
    audio_stats_update_levels_locked(GST_CLOCK_TIME_NONE);
    stats->appsrc_queued_ns = audio_stats.appsrc_queued_ns;
    stats->queue_queued_ns = audio_stats.queue_queued_ns;
    stats->pts_lead_ns = audio_stats.pts_lead_ns;
    stats->pushed_buffers = audio_stats.pushed_buffers;
    stats->appsrc_dropped = audio_stats.appsrc_dropped;
    stats->queue_dropped = audio_stats.queue_dropped;
    stats->appsrc_failures = audio_stats.appsrc_failures;
    stats->reanchors = audio_stats.reanchors;
    stats->concealed_frames = audio_stats.concealed_frames;
    stats->decoder_resets = audio_stats.decoder_resets;
    stats->latency_recoveries = audio_stats.latency_recoveries;
    stats->generation = audio_stats.generation;
    g_mutex_unlock(&audio_stats.mutex);
    return true;
}

void audio_renderer_destroy() {
    audio_renderer_stop();
    fdk_aac_eld_decoder_destroy(aac_eld_decoder);
    aac_eld_decoder = NULL;
    aac_eld = FALSE;
    for (int i = 0; i < NFORMATS ; i++ ) {
        gst_object_unref (renderer_type[i]->bus);
        renderer_type[i]->bus = NULL;
        gst_object_unref (renderer_type[i]->volume);
        renderer_type[i]->volume = NULL;
        gst_object_unref (renderer_type[i]->queue);
        renderer_type[i]->queue = NULL;
        gst_object_unref (renderer_type[i]->appsrc);
        renderer_type[i]->appsrc = NULL;
        gst_object_unref (renderer_type[i]->pipeline);
        renderer_type[i]->pipeline = NULL;
        free(renderer_type[i]);
        renderer_type[i] = NULL;
    }
}

static gboolean gstreamer_audio_pipeline_bus_callback(GstBus *bus, GstMessage *message, void *loop) {
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gchar *debug = NULL;
        gst_message_parse_error (message, &err, &debug);
        logger_log(logger, LOGGER_INFO, "GStreamer error (audio): %s %s", GST_MESSAGE_SRC_NAME(message),err->message);
        g_error_free(err);
        g_free(debug);
        if (renderer->appsrc) {
            gst_app_src_end_of_stream (GST_APP_SRC(renderer->appsrc));
        }
        gst_bus_set_flushing(bus, TRUE);
        gst_element_set_state (renderer->pipeline, GST_STATE_READY);
        g_main_loop_quit( (GMainLoop *) loop);
	break;
    }
    case GST_MESSAGE_EOS:
        logger_log(logger, LOGGER_INFO, "GStreamer: End-Of-Stream (audio)");
        break;
    case GST_MESSAGE_ELEMENT:
      // many "level" messages may be sent
        break;
    default:
        /* unhandled message */
        logger_log(logger, LOGGER_DEBUG,"GStreamer unhandled audio bus message: src = %s type = %s",
                   GST_MESSAGE_SRC_NAME(message), GST_MESSAGE_TYPE_NAME(message));
        break;
    }
    return TRUE;
}

unsigned int audio_renderer_listen(void *loop, int id) {
    g_assert(id >= 0 && id < NFORMATS);
    return (unsigned int) gst_bus_add_watch(renderer_type[id]->bus,(GstBusFunc)
                                            gstreamer_audio_pipeline_bus_callback, (gpointer) loop); 
}
