#include <stdio.h>
#include <stdlib.h>
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
/* count subframe types by instrumenting via decode loop headers not exposed;
   instead decode and report frame count + block sizes via repeated open */
int main(int argc, char** argv){
    FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char* d=malloc(sz); if(fread(d,1,sz,f)!=(size_t)sz) return 3; fclose(f);
    drflac* dec=drflac_open_memory(d,sz,NULL);
    drflac_int32* buf=malloc((size_t)dec->maxBlockSizeInPCMFrames*dec->channels*4);
    long nframes=0;
    while(drflac_read_pcm_frames_s32(dec, dec->maxBlockSizeInPCMFrames, buf)>0) nframes++;
    printf("%s frames=%ld block=%u ch=%u bps=%u\n", argv[1], nframes, dec->maxBlockSizeInPCMFrames, dec->channels, dec->bitsPerSample);
    drflac_close(dec); free(buf); free(d); return 0;
}
