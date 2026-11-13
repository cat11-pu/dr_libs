#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DR_FLAC_IMPLEMENTATION
#include "base/dr_flac.h"

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <in.flac> <iters> [out.raw]\n", argv[0]);
        return 1;
    }
    const char* path = argv[1];
    int iters = atoi(argv[2]);
    const char* outpath = argc > 3 ? argv[3] : NULL;

    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc(sz);
    if (fread(data, 1, sz, f) != (size_t)sz) { perror("fread"); return 1; }
    fclose(f);

    drflac_uint64 totalFrames = 0;
    drflac_uint32 channels = 0, sampleRate = 0;
    drflac_int32* refOut = NULL;
    size_t refCount = 0;
    double best = 1e9, sum = 0;

    for (int it = 0; it < iters; ++it) {
        drflac* dec = drflac_open_memory(data, sz, NULL);
        if (!dec) { fprintf(stderr, "open failed\n"); return 1; }
        channels = dec->channels;
        sampleRate = dec->sampleRate;
        size_t cap = (size_t)(dec->totalPCMFrameCount + 16) * channels;
        drflac_int32* out = malloc(cap * sizeof(drflac_int32));

        double t0 = now_sec();
        drflac_uint64 n = drflac_read_pcm_frames_s32(dec, dec->totalPCMFrameCount, out);
        double t1 = now_sec();

        double dt = t1 - t0;
        if (dt < best) best = dt;
        sum += dt;
        totalFrames = n;

        if (it == 0) {
            refOut = out;
            refCount = (size_t)n * channels;
        } else {
            if (n * channels != refCount || memcmp(refOut, out, refCount * sizeof(drflac_int32)) != 0) {
                fprintf(stderr, "MISMATCH across iterations at iter %d\n", it);
                return 2;
            }
            free(out);
        }
        drflac_close(dec);
    }

    if (outpath) {
        FILE* o = fopen(outpath, "wb");
        fwrite(refOut, sizeof(drflac_int32), refCount, o);
        fclose(o);
    }

    double durSec = (double)totalFrames / sampleRate;
    printf("%s frames=%llu ch=%u sr=%u dur=%.3fs best=%.4fs avg=%.4fs speedup=%.2fx bytes=%ld\n",
           path, (unsigned long long)totalFrames, channels, sampleRate, durSec,
           best, sum / iters, durSec / best, sz);

    free(refOut);
    free(data);
    return 0;
}
