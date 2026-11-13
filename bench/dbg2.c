#include <stdio.h>
#include <stdlib.h>
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n)exit(2);fclose(f);return d;}
int main(int c,char**v){
    long n;unsigned char*d=load(v[1],&n);
    drflac* dec=drflac_open_memory(d,n,NULL);
    drflac_uint64 declared=dec->totalPCMFrameCount; drflac_close(dec);
    printf("declared=%llu\n",(unsigned long long)declared);
    /* emulate 2-thread partition */
    drflac_uint64 half=declared/2;
    drflac* w0=drflac_open_memory(d,n,NULL);
    drflac_int32* b0=malloc((half+16)*2*4);
    drflac_uint64 g0=drflac_read_pcm_frames_s32(w0,half,b0);
    printf("worker0 requested=%llu got=%llu\n",(unsigned long long)half,(unsigned long long)g0);
    drflac_close(w0);
    drflac* w1=drflac_open_memory(d,n,NULL);
    int sok=drflac_seek_to_pcm_frame(w1,half);
    drflac_int32* b1=malloc((half+16)*2*4);
    drflac_uint64 g1=drflac_read_pcm_frames_s32(w1,half,b1);
    printf("worker1 seek=%d requested=%llu got=%llu\n",sok,(unsigned long long)half,(unsigned long long)g1);
    drflac_close(w1);
    return 0;
}
