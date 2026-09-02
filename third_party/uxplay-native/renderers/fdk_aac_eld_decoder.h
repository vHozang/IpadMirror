/*
 * Narrow Windows AAC-ELD adapter for UxPlay's GStreamer audio renderer.
 * libfdk-aac is resolved at runtime so its code is not statically linked into
 * the receiver. The decoder consumes one raw AirPlay AAC-ELD access unit and
 * returns interleaved signed 16-bit PCM.
 */
#ifndef FDK_AAC_ELD_DECODER_H
#define FDK_AAC_ELD_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fdk_aac_eld_decoder_s fdk_aac_eld_decoder_t;

fdk_aac_eld_decoder_t *fdk_aac_eld_decoder_create(char *error, size_t error_size);
void fdk_aac_eld_decoder_destroy(fdk_aac_eld_decoder_t *decoder);
int fdk_aac_eld_decoder_reset(fdk_aac_eld_decoder_t *decoder, char *error, size_t error_size);
int fdk_aac_eld_decoder_decode(fdk_aac_eld_decoder_t *decoder,
                               const uint8_t *data,
                               size_t data_len,
                               const int16_t **pcm,
                               size_t *sample_count,
                               int *sample_rate,
                               int *channels,
                               int *frame_size,
                               char *error,
                               size_t error_size);
int fdk_aac_eld_decoder_conceal(fdk_aac_eld_decoder_t *decoder,
                                const int16_t **pcm,
                                size_t *sample_count,
                                int *sample_rate,
                                int *channels,
                                int *frame_size,
                                char *error,
                                size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
