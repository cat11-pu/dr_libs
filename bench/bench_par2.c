#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac_opt.h"
static double now_sec(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
typedef struct { drflac* dec; drflac_uint64 start, cnt; drflac_uint32 ch; drflac_int32* base; int ok; } work_t;
static void* worker(void* a){
    work_t* w=a;
    if(w->start>0 && !drflac_seek_to_pcm_frame(w->dec,w->start)){w->ok=0;return NULL;}
    drflac_uint64 g=drflac_read_pcm_frames_s32(w->dec,w->cnt,w->base+w->start*w->ch);
    w->ok=(g==w->cnt); return NULL;
}
int main(int argc,char**argv){
    int nth=atoi(argv[2]), iters=atoi(argv[3]);
    FILE* f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char*d=malloc(sz);if(fread(d,1,sz,f)!=(size_t)sz)return 3;fclose(f);
    drflac* probe=drflac_open_memory(d,sz,NULL);
    drflac_uint64 total=probe->totalPCMFrameCount; drflac_uint32 ch=probe->channels,sr=probe->sampleRate;
    drflac_close(probe);
    drflac_int32* out=malloc((size_t)total*ch*4);
    pthread_t* th=malloc(sizeof(pthread_t)*nth); work_t* w=malloc(sizeof(work_t)*nth);
    drflac** decs=malloc(sizeof(drflac*)*nth);
    for(int t=0;t<nth;t++) decs[t]=drflac_open_memory(d,sz,NULL);
    double best=1e9,sum=0;
    for(int it=0;it<iters;it++){
        drflac_uint64 per=total/nth;
        for(int t=0;t<nth;t++){
            w[t].dec=decs[t];w[t].start=per*t;w[t].cnt=(t==nth-1)?(total-per*t):per;w[t].ch=ch;w[t].base=out;w[t].ok=1;
        }
        double t0=now_sec();
        for(int t=0;t<nth;t++)pthread_create(&th[t],NULL,worker,&w[t]);
        for(int t=0;t<nth;t++)pthread_join(th[t],NULL);
        double t1=now_sec(); double dt=t1-t0; if(dt<best)best=dt; sum+=dt;
        for(int t=0;t<nth;t++)if(!w[t].ok){fprintf(stderr,"fail\n");return 2;}
    }
    double dur=(double)total/sr;
    printf("%s threads=%d (persistent decoders, seek+decode timed) best=%.4fs avg=%.4fs speedup=%.2fx\n",argv[1],nth,best,sum/iters,dur/best);
    return 0;
}
