#include "fdk_aac_eld_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define FDK_TT_MP4_RAW 0
#define FDK_AAC_DEC_OK 0
#define FDK_AAC_DEC_CONCEAL 1
#define FDK_AAC_DEC_DECODE_ERROR_START 0x4000
#define FDK_AAC_DEC_DECODE_ERROR_END 0x4fff
#define FDK_MAX_SAMPLES_PER_FRAME 2048
#define FDK_MAX_CHANNELS 8
#define FDK_PCM_CAPACITY (FDK_MAX_SAMPLES_PER_FRAME * FDK_MAX_CHANNELS)

typedef void *(*fdk_open_fn)(int transport_format, unsigned int layers);
typedef int (*fdk_config_raw_fn)(void *decoder, unsigned char *config[], const unsigned int length[]);
typedef int (*fdk_fill_fn)(void *decoder, unsigned char *buffer[], const unsigned int buffer_size[], unsigned int *bytes_valid);
typedef int (*fdk_decode_frame_fn)(void *decoder, short *pcm, int pcm_capacity, unsigned int flags);
typedef void (*fdk_close_fn)(void *decoder);

typedef struct fdk_stream_info_s {
    int sample_rate;
    int frame_size;
    int channels;
} fdk_stream_info_t;
typedef fdk_stream_info_t *(*fdk_get_stream_info_fn)(void *decoder);

struct fdk_aac_eld_decoder_s {
#if defined(_WIN32)
    HMODULE library;
#else
    void *library;
#endif
    void *handle;
    fdk_open_fn open;
    fdk_config_raw_fn config_raw;
    fdk_fill_fn fill;
    fdk_decode_frame_fn decode_frame;
    fdk_close_fn close;
    fdk_get_stream_info_fn get_stream_info;
    int16_t pcm[FDK_PCM_CAPACITY];
};

static const unsigned char aac_eld_asc[] = {0xf8, 0xe8, 0x50, 0x00};

static void set_error(char *error, size_t error_size, const char *message, int code)
{
    if (!error || error_size == 0) {
        return;
    }
    if (code >= 0) {
        snprintf(error, error_size, "%s (code %d)", message, code);
    } else {
        snprintf(error, error_size, "%s", message);
    }
}

static void close_handle(fdk_aac_eld_decoder_t *decoder)
{
    if (decoder && decoder->handle && decoder->close) {
        decoder->close(decoder->handle);
        decoder->handle = NULL;
    }
}

static int open_handle(fdk_aac_eld_decoder_t *decoder, char *error, size_t error_size)
{
    unsigned char config_copy[sizeof(aac_eld_asc)];
    unsigned char *config[1];
    const unsigned int config_len[1] = {(unsigned int)sizeof(aac_eld_asc)};
    int result;

    close_handle(decoder);
    decoder->handle = decoder->open(FDK_TT_MP4_RAW, 1);
    if (!decoder->handle) {
        set_error(error, error_size, "aacDecoder_Open failed", -1);
        return 0;
    }

    memcpy(config_copy, aac_eld_asc, sizeof(config_copy));
    config[0] = config_copy;
    result = decoder->config_raw(decoder->handle, config, config_len);
    if (result != FDK_AAC_DEC_OK) {
        set_error(error, error_size, "aacDecoder_ConfigRaw rejected f8e85000", result);
        close_handle(decoder);
        return 0;
    }
    return 1;
}

fdk_aac_eld_decoder_t *fdk_aac_eld_decoder_create(char *error, size_t error_size)
{
#if !defined(_WIN32)
    set_error(error, error_size, "libfdk-aac AAC-ELD decoding is available only on Windows", -1);
    return NULL;
#else
    fdk_aac_eld_decoder_t *decoder = (fdk_aac_eld_decoder_t *)calloc(1, sizeof(*decoder));
    if (!decoder) {
        set_error(error, error_size, "out of memory creating AAC-ELD decoder", -1);
        return NULL;
    }

    decoder->library = LoadLibraryW(L"libfdk-aac-2.dll");
    if (!decoder->library) {
        set_error(error, error_size, "libfdk-aac-2.dll not found", -1);
        free(decoder);
        return NULL;
    }

#define RESOLVE(member, symbol) decoder->member = (fdk_##member##_fn)(void *)GetProcAddress(decoder->library, symbol)
    RESOLVE(open, "aacDecoder_Open");
    RESOLVE(config_raw, "aacDecoder_ConfigRaw");
    RESOLVE(fill, "aacDecoder_Fill");
    RESOLVE(decode_frame, "aacDecoder_DecodeFrame");
    RESOLVE(close, "aacDecoder_Close");
    RESOLVE(get_stream_info, "aacDecoder_GetStreamInfo");
#undef RESOLVE

    if (!decoder->open || !decoder->config_raw || !decoder->fill ||
        !decoder->decode_frame || !decoder->close || !decoder->get_stream_info) {
        set_error(error, error_size, "libfdk-aac-2.dll is missing required decoder symbols", -1);
        FreeLibrary(decoder->library);
        free(decoder);
        return NULL;
    }

    if (!open_handle(decoder, error, error_size)) {
        FreeLibrary(decoder->library);
        free(decoder);
        return NULL;
    }
    return decoder;
#endif
}

void fdk_aac_eld_decoder_destroy(fdk_aac_eld_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }
    close_handle(decoder);
#if defined(_WIN32)
    if (decoder->library) {
        FreeLibrary(decoder->library);
    }
#endif
    free(decoder);
}

int fdk_aac_eld_decoder_reset(fdk_aac_eld_decoder_t *decoder, char *error, size_t error_size)
{
    return decoder ? open_handle(decoder, error, error_size) : 0;
}

static int collect_output(fdk_aac_eld_decoder_t *decoder,
                          int result,
                          const int16_t **pcm,
                          size_t *sample_count,
                          int *sample_rate,
                          int *channels,
                          int *frame_size,
                          char *error,
                          size_t error_size)
{
    fdk_stream_info_t *info;
    size_t total;

    if (result != FDK_AAC_DEC_OK &&
        (result < FDK_AAC_DEC_DECODE_ERROR_START ||
         result > FDK_AAC_DEC_DECODE_ERROR_END)) {
        set_error(error, error_size, "aacDecoder_DecodeFrame failed", result);
        return 0;
    }

    info = decoder->get_stream_info(decoder->handle);
    if (!info || info->sample_rate <= 0 || info->channels <= 0 || info->frame_size <= 0) {
        set_error(error, error_size, "libfdk-aac returned invalid stream information", -1);
        return 0;
    }
    total = (size_t)info->channels * (size_t)info->frame_size;
    if (total > FDK_PCM_CAPACITY) {
        set_error(error, error_size, "libfdk-aac PCM frame exceeds adapter capacity", -1);
        return 0;
    }

    *pcm = decoder->pcm;
    *sample_count = total;
    if (sample_rate) *sample_rate = info->sample_rate;
    if (channels) *channels = info->channels;
    if (frame_size) *frame_size = info->frame_size;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int fdk_aac_eld_decoder_decode(fdk_aac_eld_decoder_t *decoder,
                               const uint8_t *data,
                               size_t data_len,
                               const int16_t **pcm,
                               size_t *sample_count,
                               int *sample_rate,
                               int *channels,
                               int *frame_size,
                               char *error,
                               size_t error_size)
{
    unsigned char *input[1];
    const unsigned int input_size[1] = {(unsigned int)data_len};
    unsigned int bytes_valid = (unsigned int)data_len;
    int result;

    if (!decoder || !decoder->handle || !data || data_len == 0 || !pcm || !sample_count) {
        set_error(error, error_size, "invalid AAC-ELD decoder input", -1);
        return 0;
    }

    input[0] = (unsigned char *)(uintptr_t)data;
    result = decoder->fill(decoder->handle, input, input_size, &bytes_valid);
    if (result != FDK_AAC_DEC_OK) {
        set_error(error, error_size, "aacDecoder_Fill failed", result);
        return 0;
    }

    result = decoder->decode_frame(decoder->handle, decoder->pcm, FDK_PCM_CAPACITY, 0);
    return collect_output(decoder, result, pcm, sample_count, sample_rate,
                          channels, frame_size, error, error_size);
}

int fdk_aac_eld_decoder_conceal(fdk_aac_eld_decoder_t *decoder,
                                const int16_t **pcm,
                                size_t *sample_count,
                                int *sample_rate,
                                int *channels,
                                int *frame_size,
                                char *error,
                                size_t error_size)
{
    int result;

    if (!decoder || !decoder->handle || !pcm || !sample_count) {
        set_error(error, error_size, "invalid AAC-ELD concealment request", -1);
        return 0;
    }

    result = decoder->decode_frame(decoder->handle, decoder->pcm,
                                   FDK_PCM_CAPACITY, FDK_AAC_DEC_CONCEAL);
    return collect_output(decoder, result, pcm, sample_count, sample_rate,
                          channels, frame_size, error, error_size);
}
