#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac_opt.h"
static double now_sec(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
int main(int argc,char**argv){
    FILE* f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char*d=malloc(sz);if(fread(d,1,sz,f)!=(size_t)sz)return 3;fclose(f);
    drflac* dec=drflac_open_memory(d,sz,NULL);
    drflac_uint64 total=dec->totalPCMFrameCount;
    double t0=now_sec();
    for(int i=0;i<100;i++){ drflac_seek_to_pcm_frame(dec, total/2); }
    double t1=now_sec();
    printf("100 seeks to midpoint: %.4f ms each\n",(t1-t0)*10.0);
    drflac_close(dec); return 0;
}
