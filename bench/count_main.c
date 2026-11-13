#include <stdio.h>
#include <stdlib.h>
#define DR_FLAC_IMPLEMENTATION
#include "/tmp/dr_count.h"
long g_sse41_calls=0, g_scalar_calls=0;
int main(int argc,char**argv){
    FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char* d=malloc(sz); if(fread(d,1,sz,f)!=(size_t)sz) return 3; fclose(f);
    drflac* dec=drflac_open_memory(d,sz,NULL);
    drflac_int32* buf=malloc((size_t)dec->totalPCMFrameCount*dec->channels*4);
    drflac_read_pcm_frames_s32(dec, dec->totalPCMFrameCount, buf);
    printf("%s sse41=%ld scalar=%ld scalar_pct=%.1f%%\n", argv[1], g_sse41_calls, g_scalar_calls,
        100.0*g_scalar_calls/(g_sse41_calls+g_scalar_calls));
    return 0;
}
