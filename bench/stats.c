#include <stdio.h>
#include <stdlib.h>
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
int main(int argc, char** argv){
    FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char* d=malloc(sz); fread(d,1,sz,f); fclose(f);
    drflac* dec=drflac_open_memory(d,sz,NULL);
    printf("ch=%u bps=%u sr=%u block=%u frames=%llu\n", dec->channels, dec->bitsPerSample, dec->sampleRate,
        dec->maxBlockSizeInPCMFrames, (unsigned long long)dec->totalPCMFrameCount);
    drflac_int32* buf=malloc(dec->totalPCMFrameCount*dec->channels*4);
    drflac_read_pcm_frames_s32(dec, dec->totalPCMFrameCount, buf);
    drflac_close(dec); free(buf); free(d); return 0;
}
