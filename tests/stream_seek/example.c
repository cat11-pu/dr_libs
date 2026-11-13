/* Minimal dr_stream_seek usage: stream a file and jump to a point in time. */

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

#define CHUNK_FRAMES 1024

int main(int argc, char** argv)
{
    drss_decoder dec;
    drwav_int16 chunk[CHUNK_FRAMES * 2];
    drwav_uint64 actualFrame;
    double actualTime;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.wav|file.flac|file.mp3> [timeSeconds]\n", argv[0]);
        return 2;
    }

    if (!drss_open_file(&dec, argv[1], drss_format_unknown)) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }

    printf("format=%d %u ch %u Hz, %llu frames (%.3f s)\n",
           (int)dec.format, dec.channels, dec.sampleRate,
           (unsigned long long)dec.totalPCMFrames,
           drss_frame_to_time(&dec, dec.totalPCMFrames));

    if (argc >= 3) {
        double target = atof(argv[2]);
        if (!drss_seek_to_time(&dec, target, &actualFrame, &actualTime)) {
            fprintf(stderr, "seek failed\n");
            drss_close(&dec);
            return 1;
        }
        printf("seeked %.6f s -> frame %llu at %.6f s (error %.3e s)\n",
               target, (unsigned long long)actualFrame, actualTime,
               actualTime - target);
    }

    /* Constant-memory streaming: same fixed chunk for the whole file. */
    for (;;) {
        drwav_uint64 framesRead = drss_read_pcm_frames_s16(&dec, CHUNK_FRAMES, chunk);
        if (framesRead == 0) {
            break;
        }
        /* Hand framesRead * dec.channels samples to the output device here. */
    }

    drss_close(&dec);
    return 0;
}
