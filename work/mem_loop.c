/* Long-running decode loop for memory-stability testing.
 *
 * Repeatedly runs the full open -> decode -> close cycle for one WAV, one
 * FLAC and one MP3 file. Every allocation goes through tracked wrappers so
 * the process can report live allocations at any iteration; an ASan build
 * additionally detects leaks and out-of-bounds accesses at exit.
 */
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION

#include "dr_wav.h"
#include "dr_flac.h"
#include "dr_mp3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static long g_live_bytes = 0;
static long g_live_allocs = 0;
static long g_peak_bytes = 0;
static long g_total_allocs = 0;

/* Each allocation carries its size in an 8-byte aligned header so free()
 * can subtract the exact byte count. User pointers start right after it. */
typedef struct
{
    size_t size;
} tracked_header;

static void* tracked_user_ptr(void* block)
{
    return (unsigned char*)block + sizeof(tracked_header);
}

static tracked_header* tracked_header_of(void* user)
{
    return (tracked_header*)((unsigned char*)user - sizeof(tracked_header));
}

static void* tracked_malloc(size_t size, void* userdata)
{
    (void)userdata;
    void* block = malloc(sizeof(tracked_header) + size);
    if (block != NULL) {
        ((tracked_header*)block)->size = size;
        g_live_bytes += (long)size;
        g_live_allocs += 1;
        g_total_allocs += 1;
        if (g_live_bytes > g_peak_bytes) {
            g_peak_bytes = g_live_bytes;
        }
    }
    return block == NULL ? NULL : tracked_user_ptr(block);
}

static void tracked_free(void* ptr, void* userdata)
{
    (void)userdata;
    if (ptr == NULL) {
        return;
    }
    tracked_header* header = tracked_header_of(ptr);
    g_live_bytes -= (long)header->size;
    g_live_allocs -= 1;
    free(header);
}

static void* tracked_realloc(void* ptr, size_t size, void* userdata)
{
    (void)userdata;
    if (ptr == NULL) {
        return tracked_malloc(size, userdata);
    }
    if (size == 0) {
        tracked_free(ptr, userdata);
        return NULL;
    }
    tracked_header* old = tracked_header_of(ptr);
    size_t old_size = old->size;
    void* block = realloc(old, sizeof(tracked_header) + size);
    if (block != NULL) {
        g_live_bytes += (long)size - (long)old_size;
        ((tracked_header*)block)->size = size;
        g_total_allocs += 1;
        if (g_live_bytes > g_peak_bytes) {
            g_peak_bytes = g_live_bytes;
        }
    }
    return block == NULL ? NULL : tracked_user_ptr(block);
}

static drwav_allocation_callbacks s_wav_cb = {
    NULL, tracked_malloc, tracked_realloc, tracked_free
};
static drflac_allocation_callbacks s_flac_cb = {
    NULL, tracked_malloc, tracked_realloc, tracked_free
};
static drmp3_allocation_callbacks s_mp3_cb = {
    NULL, tracked_malloc, tracked_realloc, tracked_free
};

static int decode_wav(const char* path)
{
    drwav wav;
    if (!drwav_init_file(&wav, path, &s_wav_cb)) {
        return -1;
    }
    drwav_int32 frames[1024];
    drwav_uint64 total = 0;
    drwav_uint64 got;
    while ((got = drwav_read_pcm_frames_s32(&wav, 1024 / wav.channels, frames)) > 0) {
        total += got;
    }
    drwav_uninit(&wav);
    return (int)(total > 0);
}

static int decode_flac(const char* path)
{
    drflac* flac = drflac_open_file(path, &s_flac_cb);
    if (flac == NULL) {
        return -1;
    }
    drflac_int32 frames[1024];
    drflac_uint64 total = 0;
    drflac_uint64 got;
    while ((got = drflac_read_pcm_frames_s32(flac, 1024 / flac->channels, frames)) > 0) {
        total += got;
    }
    drflac_close(flac);
    return (int)(total > 0);
}

static int decode_mp3(const char* path)
{
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, path, &s_mp3_cb)) {
        return -1;
    }
    drmp3_int16 frames[1024];
    drmp3_uint64 total = 0;
    drmp3_uint64 got;
    while ((got = drmp3_read_pcm_frames_s16(&mp3, 512 / mp3.channels, frames)) > 0) {
        total += got;
    }
    drmp3_uninit(&mp3);
    return (int)(total > 0);
}

int main(int argc, char** argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <iterations> <wav> <flac> <mp3>\n", argv[0]);
        return 2;
    }
    long iterations = strtol(argv[1], NULL, 10);
    const char* paths[3] = {argv[2], argv[3], argv[4]};

    long baseline_bytes = -1;
    long baseline_allocs = -1;
    long checkpoints[3] = {iterations / 4, iterations / 2, iterations};
    int next_checkpoint = 0;
    long failures = 0;

    for (long it = 1; it <= iterations; ++it) {
        if (decode_wav(paths[0]) <= 0) failures++;
        if (decode_flac(paths[1]) <= 0) failures++;
        if (decode_mp3(paths[2]) <= 0) failures++;

        if (it == 1) {
            baseline_bytes = g_live_bytes;
            baseline_allocs = g_live_allocs;
        }
        if (next_checkpoint < 3 && it == checkpoints[next_checkpoint]) {
            printf("iter=%ld live_allocs=%ld live_bytes=%ld "
                   "(delta_vs_iter1: allocs=%+ld bytes=%+ld) total_allocs=%ld\n",
                   it, g_live_allocs, g_live_bytes,
                   g_live_allocs - baseline_allocs,
                   g_live_bytes - baseline_bytes, g_total_allocs);
            next_checkpoint++;
        }
    }

    printf("peak_live_bytes=%ld decode_failures=%ld\n", g_peak_bytes, failures);
    if (g_live_allocs != 0 || g_live_bytes != 0) {
        fprintf(stderr, "LEAK: %ld live allocations, %ld live bytes at exit\n",
                g_live_allocs, g_live_bytes);
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
