/* Per-file behavior probe for the malformed corpus.
 *
 * For every input path it prints one SSV row:
 *   <path> <dr-open> <dr-any> <dr-complete> <ref-open> <ref-any> <ref-complete>
 * "any" means at least one PCM frame was produced; "complete" means the
 * decoder reached its normal end-of-stream state without a hard error.
 *
 * dr_*   : the decoder under test (dr_wav / dr_flac / dr_mp3).
 * ref    : the independent reference decoder for that format
 *           (libsndfile / libFLAC / libmpg123).
 */
#include <sndfile.h>
#include <FLAC/stream_decoder.h>
#include <mpg123.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ends_with(const char* path, const char* suffix)
{
    size_t lp = strlen(path);
    size_t ls = strlen(suffix);
    return lp >= ls && strcmp(path + lp - ls, suffix) == 0;
}

/* ------------------------------- WAV ------------------------------- */

static void probe_wav(const char* path)
{
    drwav wav;
    int dr_open = drwav_init_file(&wav, path, NULL) ? 1 : 0;
    int dr_any = 0;
    int dr_complete = 0;
    if (dr_open) {
        drwav_int32 frames[512];
        drwav_uint64 got;
        drwav_uint64 total = 0;
        while ((got = drwav_read_pcm_frames_s32(&wav, 512 / wav.channels, frames)) > 0) {
            total += got;
        }
        dr_any = (total > 0) ? 1 : 0;
        /* A short read against the declared frame count means a hard decode error. */
        dr_complete = (total >= wav.totalPCMFrameCount) ? 1 : 0;
        drwav_uninit(&wav);
    }

    SF_INFO info;
    memset(&info, 0, sizeof(info));
    SNDFILE* sf = sf_open(path, SFM_READ, &info);
    int ref_open = (sf != NULL) ? 1 : 0;
    int ref_any = 0;
    int ref_complete = 0;
    if (sf != NULL) {
        short tmp[512];
        sf_count_t got;
        sf_count_t total = 0;
        while ((got = sf_read_short(sf, tmp, 512)) > 0) {
            total += got;
        }
        ref_any = (total > 0) ? 1 : 0;
        ref_complete = (sf_error(sf) == SF_ERR_NO_ERROR) ? 1 : 0;
        sf_close(sf);
    }

    printf("%s %d %d %d %d %d %d\n", path, dr_open, dr_any, dr_complete,
           ref_open, ref_any, ref_complete);
}

/* ------------------------------- FLAC ------------------------------ */

typedef struct
{
    int got_any;
    int error;
} flac_probe_state;

static FLAC__StreamDecoderWriteStatus flac_probe_write(const FLAC__StreamDecoder* d, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* user)
{
    (void)d; (void)frame; (void)buffer;
    ((flac_probe_state*)user)->got_any = 1;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_probe_error(const FLAC__StreamDecoder* d, FLAC__StreamDecoderErrorStatus status, void* user)
{
    (void)d; (void)status;
    ((flac_probe_state*)user)->error = 1;
}

static void probe_flac(const char* path)
{
    drflac* flac = drflac_open_file(path, NULL);
    int dr_open = (flac != NULL) ? 1 : 0;
    int dr_any = 0;
    int dr_complete = 0;
    if (flac != NULL) {
        drflac_int32 frames[512];
        drflac_uint64 got;
        drflac_uint64 total = 0;
        while ((got = drflac_read_pcm_frames_s32(flac, 512 / flac->channels, frames)) > 0) {
            total += got;
        }
        dr_any = (total > 0) ? 1 : 0;
        dr_complete = (total >= flac->totalPCMFrameCount && flac->totalPCMFrameCount > 0) ? 1 : 0;
        drflac_close(flac);
    }

    FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
    flac_probe_state state;
    state.got_any = 0;
    state.error = 0;
    FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_file(
        dec, path, flac_probe_write, NULL, flac_probe_error, &state);
    int ref_open = (init_status == FLAC__STREAM_DECODER_INIT_STATUS_OK) ? 1 : 0;
    int ref_any = 0;
    int ref_complete = 0;
    if (ref_open) {
        FLAC__bool ok = FLAC__stream_decoder_process_until_end_of_stream(dec);
        ref_any = state.got_any;
        ref_complete = (ok && !state.error &&
                        FLAC__stream_decoder_get_state(dec) == FLAC__STREAM_DECODER_END_OF_STREAM) ? 1 : 0;
    }
    FLAC__stream_decoder_delete(dec);

    printf("%s %d %d %d %d %d %d\n", path, dr_open, dr_any, dr_complete,
           ref_open, ref_any, ref_complete);
}

/* ------------------------------- MP3 ------------------------------- */

static void probe_mp3(const char* path)
{
    drmp3 mp3;
    int dr_open = drmp3_init_file(&mp3, path, NULL) ? 1 : 0;
    int dr_any = 0;
    int dr_complete = 0;
    if (dr_open) {
        drmp3_int16 frames[512];
        drmp3_uint64 got;
        drmp3_uint64 total = 0;
        while ((got = drmp3_read_pcm_frames_s16(&mp3, 256 / mp3.channels, frames)) > 0) {
            total += got;
        }
        dr_any = (total > 0) ? 1 : 0;
        dr_complete = (total > 0) ? 1 : 0;
        drmp3_uninit(&mp3);
    }

    int err = 0;
    mpg123_handle* mh = mpg123_new(NULL, &err);
    int ref_open = 0;
    int ref_any = 0;
    int ref_complete = 0;
    if (mh != NULL) {
        mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0);
        if (mpg123_open(mh, path) == MPG123_OK) {
            ref_open = 1;
            unsigned char tmp[2048];
            size_t done = 0;
            int status = mpg123_read(mh, tmp, sizeof(tmp), &done);
            if (done > 0) {
                ref_any = 1;
            }
            while (status == MPG123_OK || status == MPG123_NEW_FORMAT) {
                status = mpg123_read(mh, tmp, sizeof(tmp), &done);
                if (done > 0) {
                    ref_any = 1;
                }
            }
            ref_complete = (status == MPG123_DONE && ref_any) ? 1 : 0;
        }
        mpg123_close(mh);
        mpg123_delete(mh);
    }

    printf("%s %d %d %d %d %d %d\n", path, dr_open, dr_any, dr_complete,
           ref_open, ref_any, ref_complete);
}

int main(int argc, char** argv)
{
    int i;
    mpg123_init();
    for (i = 1; i < argc; ++i) {
        if (ends_with(argv[i], ".wav")) {
            probe_wav(argv[i]);
        } else if (ends_with(argv[i], ".flac")) {
            probe_flac(argv[i]);
        } else if (ends_with(argv[i], ".mp3")) {
            probe_mp3(argv[i]);
        }
    }
    mpg123_exit();
    return 0;
}
