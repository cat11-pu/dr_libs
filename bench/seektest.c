#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n)exit(2);fclose(f);return d;}
int main(int c,char**v){
    long n;unsigned char*d=load(v[1],&n);
    drflac* dec=drflac_open_memory(d,n,NULL);
    printf("total=%llu ch=%u firstFramePos=%llu seekpts?\n",(unsigned long long)dec->totalPCMFrameCount,dec->channels,(unsigned long long)dec->firstFLACFramePosInBytes);
    drflac_int32 full[16384*2]; drflac_uint64 nf=drflac_read_pcm_frames_s32(dec,dec->totalPCMFrameCount,full);
    drflac_close(dec);
    /* now seek decode half */
    drflac* d2=drflac_open_memory(d,n,NULL);
    drflac_bool32 ok=drflac_seek_to_pcm_frame(d2, 8192);
    printf("seek ret=%d currentPCM=%llu\n",ok,(unsigned long long)d2->currentPCMFrame);
    drflac_int32 half[16384*2]; drflac_uint64 nh=drflac_read_pcm_frames_s32(d2,8192,half);
    printf("half frames=%llu\n",(unsigned long long)nh);
    int diff=memcmp(full+8192*2,half,nh*2*4);
    printf("seeked-half vs full[8192..]: %s\n",diff?"MISMATCH":"EXACT");
    drflac_close(d2); free(d); return 0;
}
