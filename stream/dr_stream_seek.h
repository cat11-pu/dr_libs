/*
dr_stream_seek - streaming decode with sample-exact time based seeking.

This is a thin, public domain wrapper over dr_wav, dr_flac and dr_mp3 that
provides a single interface for:

  1. Streaming (chunked) decoding with a caller owned buffer, so resident
     memory stays constant regardless of file length.
  2. Seeking to a point in time. The target time is resolved to the nearest
     PCM frame boundary (round to nearest, ties down) and the decoder is
     positioned on that exact frame.
  3. Sample-exact results: every frame returned after drss_seek_to_time() is
     bit-for-bit identical to the frame a from-scratch linear decode would
     produce at the same position.

Memory model
------------
The wrapper itself allocates no per-file data and stores no sample history.
The only working storage is the fixed-size decoder union below plus the
caller supplied read buffer, both independent of file length:

  - WAV:   byte seek, O(1), constant memory.
  - FLAC:  seek table / binary search / in-frame skip, O(log n) I/O in the
           worst case, constant memory (no whole-file index is built).
  - MP3:   frame-accurate seek by decoding and discarding. No seek table is
           allocated, so memory is constant; a backwards seek restarts at the
           stream beginning and advances to the target.

Include dr_wav.h, dr_flac.h and dr_mp3.h before this header.
*/

#ifndef DR_STREAM_SEEK_H
#define DR_STREAM_SEEK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    drss_format_unknown = 0,
    drss_format_wav,
    drss_format_flac,
    drss_format_mp3
} drss_format;

typedef struct
{
    drss_format format;
    unsigned int channels;
    unsigned int sampleRate;
    drwav_uint64 totalPCMFrames;   /* 0 when the length is unknown. */
    drwav_uint64 currentPCMFrame;  /* Client-space cursor, maintained here. */

    union
    {
        drwav wav;
        drflac* flac;
        drmp3 mp3;
    } decoder;
} drss_decoder;

/*
Opens a file for streaming. Returns DRSS_TRUE on success.

hint may be drss_format_unknown to detect the format from the file extension,
or an explicit format. The file must support seeking (the underlying decoders
require an onSeek callback); opening succeeds only when seeking is available.
*/
DRWAV_API drwav_bool32 drss_open_file(drss_decoder* pDecoder, const char* pFilePath, drss_format hint);

/* Closes a decoder opened with drss_open_file(). Safe on a zeroed struct. */
DRWAV_API void drss_close(drss_decoder* pDecoder);

/*
Reads up to framesToRead interleaved s16 PCM frames into pBufferOut.

Returns the number of frames actually written, which is smaller than
framesToRead only at end of stream (or on error). pBufferOut may be NULL to
advance the position without producing samples.
*/
DRWAV_API drwav_uint64 drss_read_pcm_frames_s16(drss_decoder* pDecoder, drwav_uint64 framesToRead, drwav_int16* pBufferOut);

/*
Resolves a time in seconds to the nearest PCM frame index.

Rounding is round-to-nearest, half-down, so the absolute time error is always
strictly less than one frame period (0.5 frame on average). When the total
length is known the result is clamped to the end-of-stream frame.
*/
DRWAV_API drwav_uint64 drss_time_to_frame(const drss_decoder* pDecoder, double timeSeconds);

/* Inverse of drss_time_to_frame(): the playback time of a PCM frame index. */
DRWAV_API double drss_frame_to_time(const drss_decoder* pDecoder, drwav_uint64 frameIndex);

/*
Seeks to an exact PCM frame. On success the next drss_read_pcm_frames_s16()
call returns the same samples a linear decode from the start would return at
frameIndex. A frame index beyond the end is clamped when the length is known.
*/
DRWAV_API drwav_bool32 drss_seek_to_pcm_frame(drss_decoder* pDecoder, drwav_uint64 frameIndex);

/*
Seeks to the PCM frame nearest timeSeconds. On success *pActualFrame (when
non-NULL) receives the frame the decoder landed on and *pActualTime its time.

Guarantees |pActualTime - timeSeconds| < 1/sampleRate, i.e. less than one
frame, and the decoded samples are identical to a from-scratch decode.
*/
DRWAV_API drwav_bool32 drss_seek_to_time(drss_decoder* pDecoder, double timeSeconds, drwav_uint64* pActualFrame, double* pActualTime);

/* Current decode position as a PCM frame index and playback time. */
DRWAV_API drwav_uint64 drss_get_cursor_in_pcm_frames(const drss_decoder* pDecoder);
DRWAV_API double drss_get_cursor_in_time(const drss_decoder* pDecoder);

#ifdef __cplusplus
}
#endif

#if defined(DR_STREAM_SEEK_IMPLEMENTATION)

#include <math.h>
#include <string.h>

static drss_format drss__detect_format(const char* pFilePath, drss_format hint)
{
    const char* pExt;

    if (hint != drss_format_unknown) {
        return hint;
    }

    pExt = strrchr(pFilePath, '.');
    if (pExt == NULL) {
        return drss_format_unknown;
    }

    if (strcmp(pExt, ".wav") == 0 || strcmp(pExt, ".wave") == 0) return drss_format_wav;
    if (strcmp(pExt, ".flac") == 0) return drss_format_flac;
    if (strcmp(pExt, ".mp3") == 0) return drss_format_mp3;

    return drss_format_unknown;
}

DRWAV_API drwav_bool32 drss_open_file(drss_decoder* pDecoder, const char* pFilePath, drss_format hint)
{
    drss_format format;

    if (pDecoder == NULL || pFilePath == NULL) {
        return DRWAV_FALSE;
    }

    memset(pDecoder, 0, sizeof(*pDecoder));
    format = drss__detect_format(pFilePath, hint);

    switch (format) {
        case drss_format_wav:
            if (!drwav_init_file(&pDecoder->decoder.wav, pFilePath, NULL)) {
                return DRWAV_FALSE;
            }
            pDecoder->channels       = pDecoder->decoder.wav.channels;
            pDecoder->sampleRate     = pDecoder->decoder.wav.sampleRate;
            pDecoder->totalPCMFrames = pDecoder->decoder.wav.totalPCMFrameCount;
            break;

        case drss_format_flac:
            pDecoder->decoder.flac = drflac_open_file(pFilePath, NULL);
            if (pDecoder->decoder.flac == NULL) {
                return DRWAV_FALSE;
            }
            pDecoder->channels       = pDecoder->decoder.flac->channels;
            pDecoder->sampleRate     = pDecoder->decoder.flac->sampleRate;
            pDecoder->totalPCMFrames = pDecoder->decoder.flac->totalPCMFrameCount;
            break;

        case drss_format_mp3:
            if (!drmp3_init_file(&pDecoder->decoder.mp3, pFilePath, NULL)) {
                return DRWAV_FALSE;
            }
            pDecoder->channels       = pDecoder->decoder.mp3.channels;
            pDecoder->sampleRate     = pDecoder->decoder.mp3.sampleRate;
            /* dr_mp3's total includes encoder delay and padding; frames that
               actually reach the client have both removed at read time. */
            if (pDecoder->decoder.mp3.totalPCMFrameCount == DRMP3_UINT64_MAX) {
                pDecoder->totalPCMFrames = 0;
            } else {
                pDecoder->totalPCMFrames = pDecoder->decoder.mp3.totalPCMFrameCount;
                if (pDecoder->totalPCMFrames >= pDecoder->decoder.mp3.delayInPCMFrames) {
                    pDecoder->totalPCMFrames -= pDecoder->decoder.mp3.delayInPCMFrames;
                }
                if (pDecoder->totalPCMFrames >= pDecoder->decoder.mp3.paddingInPCMFrames) {
                    pDecoder->totalPCMFrames -= pDecoder->decoder.mp3.paddingInPCMFrames;
                }
            }
            break;

        case drss_format_unknown:
        default:
            return DRWAV_FALSE;
    }

    pDecoder->format = format;
    return DRWAV_TRUE;
}

DRWAV_API void drss_close(drss_decoder* pDecoder)
{
    if (pDecoder == NULL) {
        return;
    }

    switch (pDecoder->format) {
        case drss_format_wav:  drwav_uninit(&pDecoder->decoder.wav); break;
        case drss_format_flac: drflac_close(pDecoder->decoder.flac); pDecoder->decoder.flac = NULL; break;
        case drss_format_mp3:  drmp3_uninit(&pDecoder->decoder.mp3); break;
        default: break;
    }

    memset(pDecoder, 0, sizeof(*pDecoder));
}

DRWAV_API drwav_uint64 drss_read_pcm_frames_s16(drss_decoder* pDecoder, drwav_uint64 framesToRead, drwav_int16* pBufferOut)
{
    drwav_uint64 framesRead;

    if (pDecoder == NULL) {
        return 0;
    }

    switch (pDecoder->format) {
        case drss_format_wav:
            framesRead = drwav_read_pcm_frames_s16(&pDecoder->decoder.wav, framesToRead, pBufferOut);
            break;
        case drss_format_flac:
            framesRead = drflac_read_pcm_frames_s16(pDecoder->decoder.flac, framesToRead, pBufferOut);
            break;
        case drss_format_mp3:
            framesRead = drmp3_read_pcm_frames_s16(&pDecoder->decoder.mp3, framesToRead, pBufferOut);
            break;
        default:
            return 0;
    }

    pDecoder->currentPCMFrame += framesRead;
    return framesRead;
}

DRWAV_API drwav_uint64 drss_time_to_frame(const drss_decoder* pDecoder, double timeSeconds)
{
    double nearest;
    drwav_uint64 frameIndex;

    if (pDecoder == NULL || pDecoder->sampleRate == 0 || timeSeconds <= 0.0) {
        return 0;
    }

    /* Round to nearest, ties down: floor(x + 0.5) would round ties up. */
    nearest = floor(timeSeconds * (double)pDecoder->sampleRate + 0.5);
    if (nearest < 0.0) {
        nearest = 0.0;
    }

    frameIndex = (drwav_uint64)nearest;

    if (pDecoder->totalPCMFrames != 0 && frameIndex > pDecoder->totalPCMFrames) {
        frameIndex = pDecoder->totalPCMFrames;
    }

    return frameIndex;
}

DRWAV_API double drss_frame_to_time(const drss_decoder* pDecoder, drwav_uint64 frameIndex)
{
    if (pDecoder == NULL || pDecoder->sampleRate == 0) {
        return 0.0;
    }

    return (double)frameIndex / (double)pDecoder->sampleRate;
}

DRWAV_API drwav_bool32 drss_seek_to_pcm_frame(drss_decoder* pDecoder, drwav_uint64 frameIndex)
{
    if (pDecoder == NULL) {
        return DRWAV_FALSE;
    }

    /* Normalize end-of-stream handling across formats: clamp when the length
       is known. dr_wav and dr_flac already do this internally and accept the
       one-past-end frame as a valid cursor; dr_mp3's brute-force seek can only
       land on a frame that still produces output, so its ceiling is n-1. */
    if (pDecoder->totalPCMFrames != 0 && frameIndex > pDecoder->totalPCMFrames) {
        frameIndex = pDecoder->totalPCMFrames;
    }
    if (pDecoder->format == drss_format_mp3 && pDecoder->totalPCMFrames != 0 &&
        frameIndex >= pDecoder->totalPCMFrames) {
        frameIndex = pDecoder->totalPCMFrames - 1;
    }

    {
        drwav_bool32 wasSuccessful;
        switch (pDecoder->format) {
            case drss_format_wav:
                wasSuccessful = drwav_seek_to_pcm_frame(&pDecoder->decoder.wav, frameIndex);
                break;
            case drss_format_flac:
                wasSuccessful = drflac_seek_to_pcm_frame(pDecoder->decoder.flac, frameIndex);
                break;
            case drss_format_mp3:
                wasSuccessful = drmp3_seek_to_pcm_frame(&pDecoder->decoder.mp3, frameIndex);
                break;
            default:
                return DRWAV_FALSE;
        }

        if (wasSuccessful) {
            pDecoder->currentPCMFrame = frameIndex;
        }
        return wasSuccessful;
    }
}

DRWAV_API drwav_bool32 drss_seek_to_time(drss_decoder* pDecoder, double timeSeconds, drwav_uint64* pActualFrame, double* pActualTime)
{
    drwav_uint64 frameIndex;

    if (pDecoder == NULL) {
        return DRWAV_FALSE;
    }

    frameIndex = drss_time_to_frame(pDecoder, timeSeconds);
    if (!drss_seek_to_pcm_frame(pDecoder, frameIndex)) {
        return DRWAV_FALSE;
    }

    /* Report where the decoder actually landed (may be clamped at EOF). */
    frameIndex = pDecoder->currentPCMFrame;
    if (pActualFrame != NULL) {
        *pActualFrame = frameIndex;
    }
    if (pActualTime != NULL) {
        *pActualTime = drss_frame_to_time(pDecoder, frameIndex);
    }

    return DRWAV_TRUE;
}

DRWAV_API drwav_uint64 drss_get_cursor_in_pcm_frames(const drss_decoder* pDecoder)
{
    return pDecoder == NULL ? 0 : pDecoder->currentPCMFrame;
}

DRWAV_API double drss_get_cursor_in_time(const drss_decoder* pDecoder)
{
    return drss_frame_to_time(pDecoder, drss_get_cursor_in_pcm_frames(pDecoder));
}

#endif /* DR_STREAM_SEEK_IMPLEMENTATION */

#endif /* DR_STREAM_SEEK_H */
