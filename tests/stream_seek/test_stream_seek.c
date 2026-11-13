/*
Consistency test for dr_stream_seek.

For every input file this program:

  1. Decodes the whole file linearly in fixed-size chunks (the reference),
     streaming through a constant-size scratch buffer.
  2. For a set of seek times (edge cases plus pseudo-random interior points),
     seeks with drss_seek_to_time() and compares every subsequent sample
     against the reference buffer. The compare runs through end of stream.
  3. Reports the time error of every seek and verifies it stays below one
     frame period.

The reference buffer is the only file-sized allocation; the decoder itself
works out of a fixed chunk, which is also asserted via RSS measurements on
Linux.

Usage: test_stream_seek <file.wav|file.flac|file.mp3> [...]
*/

#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_STREAM_SEEK_IMPLEMENTATION

#include "../../dr_wav.h"
#include "../../dr_flac.h"
#include "../../dr_mp3.h"
#include "../../stream/dr_stream_seek.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__linux__)
#include <sys/resource.h>
static long rss_kb(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}
#else
static long rss_kb(void) { return 0; }
#endif

#define CHUNK_FRAMES 512

typedef struct
{
    unsigned int channels;
    unsigned int sampleRate;
    unsigned long long totalFrames;
    drwav_int16* reference;   /* interleaved, totalFrames*channels */
} linear_reference;

static int build_reference(const char* pFilePath, linear_reference* pRef)
{
    drss_decoder dec;
    drwav_int16 chunk[CHUNK_FRAMES * 8];
    unsigned long long framesStored = 0;
    unsigned long long capacity = 0;
    drwav_uint64 framesRead;

    memset(pRef, 0, sizeof(*pRef));
    if (!drss_open_file(&dec, pFilePath, drss_format_unknown)) {
        fprintf(stderr, "FAIL %s: cannot open\n", pFilePath);
        return 0;
    }

    pRef->channels   = dec.channels;
    pRef->sampleRate = dec.sampleRate;

    while ((framesRead = drss_read_pcm_frames_s16(&dec, CHUNK_FRAMES, chunk)) > 0) {
        unsigned long long samples = framesRead * dec.channels;
        if (framesStored + framesRead > capacity) {
            unsigned long long newCapacity = capacity == 0 ? 65536 : capacity * 2;
            drwav_int16* pNew = (drwav_int16*)realloc(pRef->reference, (size_t)(newCapacity * dec.channels) * sizeof(drwav_int16));
            if (pNew == NULL) {
                fprintf(stderr, "FAIL %s: out of memory\n", pFilePath);
                drss_close(&dec);
                free(pRef->reference);
                return 0;
            }
            pRef->reference = pNew;
            capacity = newCapacity;
        }

        memcpy(pRef->reference + framesStored * dec.channels, chunk, (size_t)samples * sizeof(drwav_int16));
        framesStored += framesRead;
    }

    pRef->totalFrames = framesStored;
    drss_close(&dec);
    return 1;
}

/* Deterministic LCG so the same seek points are used for every format. */
static unsigned long long lcg_state;
static unsigned long long lcg_next(unsigned long long mod)
{
    lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (lcg_state >> 33) % mod;
}

static int test_one_file(const char* pFilePath)
{
    linear_reference ref;
    drss_decoder dec;
    double duration;
    double seekTimes[24];
    int seekCount = 0;
    int i;
    int failures = 0;
    long rssOpenKb, rssAfterSeeksKb;

    if (!build_reference(pFilePath, &ref)) {
        return 1;
    }

    duration = (double)ref.totalFrames / (double)ref.sampleRate;
    printf("== %s  %u Hz, %u ch, %llu frames (%.6f s)\n",
           pFilePath, ref.sampleRate, ref.channels, ref.totalFrames, duration);

    if (!drss_open_file(&dec, pFilePath, drss_format_unknown)) {
        fprintf(stderr, "FAIL %s: second open failed\n", pFilePath);
        free(ref.reference);
        return 1;
    }

    /* Edge cases first: 0, half frame, one frame, last frame, past the end. */
    seekTimes[seekCount++] = 0.0;
    seekTimes[seekCount++] = 0.5 / (double)ref.sampleRate;
    seekTimes[seekCount++] = 1.0 / (double)ref.sampleRate;
    seekTimes[seekCount++] = duration;
    seekTimes[seekCount++] = duration + 10.0;

    lcg_state = 0x123456789abcdef0ULL;
    for (i = 0; i < 19 && seekCount < (int)(sizeof(seekTimes)/sizeof(seekTimes[0])); ++i) {
        double fraction = (double)(lcg_next(1000000) + 1) / 1000001.0;  /* (0,1) */
        seekTimes[seekCount++] = fraction * duration;
    }

    rssOpenKb = rss_kb();

    for (i = 0; i < seekCount; ++i) {
        double targetTime = seekTimes[i];
        unsigned long long actualFrame = 0;
        double actualTime = 0.0;
        double timeError;
        drwav_int16 chunk[CHUNK_FRAMES * 8];
        unsigned long long comparedFrames = 0;
        drwav_uint64 framesRead;
        unsigned long long sampleMismatches = 0;
        long long firstMismatch = -1;

        if (!drss_seek_to_time(&dec, targetTime, &actualFrame, &actualTime)) {
            printf("  seek t=%10.6f s -> FAILED\n", targetTime);
            failures++;
            continue;
        }

        timeError = fabs(actualTime - targetTime);

        while ((framesRead = drss_read_pcm_frames_s16(&dec, CHUNK_FRAMES, chunk)) > 0) {
            unsigned long long s;
            for (s = 0; s < (unsigned long long)framesRead * dec.channels; ++s) {
                unsigned long long globalFrame = actualFrame + comparedFrames + s / dec.channels;
                if (globalFrame >= ref.totalFrames) {
                    printf("  seek t=%10.6f s -> decoder ran past reference at frame %llu\n",
                           targetTime, globalFrame);
                    failures++;
                    goto stop_compare;
                }
                if (chunk[s] != ref.reference[globalFrame * dec.channels + (s % dec.channels)]) {
                    if (firstMismatch < 0) {
                        firstMismatch = (long long)(globalFrame);
                    }
                    sampleMismatches++;
                }
            }
            comparedFrames += framesRead;
        }
stop_compare:

        {
            double maxError = 1.0 / (double)ref.sampleRate;
            /* A target at or past the end is specified to clamp to the final
               readable frame (one-past-end for WAV/FLAC, n-1 for MP3). */
            int clamped = targetTime >= duration;
            int errorOk = clamped ? actualFrame + 1 >= ref.totalFrames : timeError < maxError;
            unsigned long long expectedFrames =
                actualFrame <= ref.totalFrames ? ref.totalFrames - actualFrame : 0;
            int coverageOk = comparedFrames == expectedFrames;

            if (!errorOk || sampleMismatches != 0 || !coverageOk) {
                failures++;
            }

            printf("  seek t=%10.6f -> frame %8llu (t=%10.6f, err=%.3e s, %s) "
                   "frames=%llu/%llu mismatches=%llu%s\n",
                   targetTime, actualFrame, actualTime, timeError,
                   errorOk ? (clamped ? "clamp-to-end OK" : "err<1frame OK") : "ERR TOO LARGE",
                   comparedFrames, expectedFrames, sampleMismatches,
                   firstMismatch >= 0 ? "  <-- MISMATCH" : "");
        }
    }

    rssAfterSeeksKb = rss_kb();

    /* Constant-memory check: repeated seeks must not grow RSS beyond noise. */
    if (rssOpenKb > 0) {
        long growth = rssAfterSeeksKb - rssOpenKb;
        printf("  RSS: open=%ld KB, after %d seeks=%ld KB (growth %ld KB)\n",
               rssOpenKb, seekCount, rssAfterSeeksKb, growth);
        if (growth > 2048) {
            printf("  FAIL: memory grew by %ld KB across seeks\n", growth);
            failures++;
        }
    }

    drss_close(&dec);
    free(ref.reference);

    printf("  %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures;
}

int main(int argc, char** argv)
{
    int i;
    int totalFailures = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <audio file>...\n", argv[0]);
        return 2;
    }

    for (i = 1; i < argc; ++i) {
        totalFailures += test_one_file(argv[i]);
    }

    printf("\n%s (%d failing file(s))\n", totalFailures == 0 ? "ALL PASS" : "SOME FAILURES", totalFailures);
    return totalFailures == 0 ? 0 : 1;
}
