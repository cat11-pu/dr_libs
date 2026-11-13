#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac_opt.h"

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

typedef struct {
    const unsigned char* data;
    size_t size;
    drflac_uint64 startFrame;
    drflac_uint64 frameCount;
    drflac_uint32 channels;
    drflac_int32* outBase;   /* interleaved, full stream */
    int ok;
} work_t;

static void* worker(void* arg) {
    work_t* w = (work_t*)arg;
    drflac* dec = drflac_open_memory(w->data, w->size, NULL);
    if (!dec) { w->ok = 0; return NULL; }
    if (w->startFrame > 0) {
        if (!drflac_seek_to_pcm_frame(dec, w->startFrame)) { w->ok = 0; drflac_close(dec); return NULL; }
    }
    drflac_int32* dst = w->outBase + (w->startFrame * w->channels);
    drflac_uint64 got = drflac_read_pcm_frames_s32(dec, w->frameCount, dst);
    w->ok = (got == w->frameCount);
    drflac_close(dec);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <in.flac> <threads> <iters> [out.raw]\n", argv[0]);
        return 1;
    }
    const char* path = argv[1];
    int nth = atoi(argv[2]);
    int iters = atoi(argv[3]);
    const char* outpath = argc > 4 ? argv[4] : NULL;

    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc(sz);
    if (fread(data, 1, sz, f) != (size_t)sz) { perror("fread"); return 1; }
    fclose(f);

    drflac* probe = drflac_open_memory(data, sz, NULL);
    drflac_uint64 total = probe->totalPCMFrameCount;
    drflac_uint32 ch = probe->channels, sr = probe->sampleRate;
    drflac_close(probe);

    drflac_int32* out = malloc((size_t)total * ch * sizeof(drflac_int32));
    pthread_t* threads = malloc(sizeof(pthread_t) * nth);
    work_t* work = malloc(sizeof(work_t) * nth);

    double best = 1e9, sum = 0;
    for (int it = 0; it < iters; ++it) {
        drflac_uint64 per = total / nth;
        for (int t = 0; t < nth; ++t) {
            work[t].data = data; work[t].size = sz; work[t].channels = ch; work[t].outBase = out;
            work[t].startFrame = per * t;
            work[t].frameCount = (t == nth - 1) ? (total - work[t].startFrame) : per;
            work[t].ok = 1;
        }
        double t0 = now_sec();
        for (int t = 0; t < nth; ++t) pthread_create(&threads[t], NULL, worker, &work[t]);
        for (int t = 0; t < nth; ++t) pthread_join(threads[t], NULL);
        double t1 = now_sec();
        double dt = t1 - t0;
        if (dt < best) best = dt;
        sum += dt;
        for (int t = 0; t < nth; ++t) if (!work[t].ok) { fprintf(stderr, "worker %d failed\n", t); return 2; }
    }

    if (outpath) {
        FILE* o = fopen(outpath, "wb");
        fwrite(out, sizeof(drflac_int32), (size_t)total * ch, o);
        fclose(o);
    }

    double durSec = (double)total / sr;
    printf("%s threads=%d dur=%.3fs best=%.4fs avg=%.4fs speedup=%.2fx\n",
           path, nth, durSec, best, sum / iters, durSec / best);

    free(work); free(threads); free(out); free(data);
    return 0;
}
