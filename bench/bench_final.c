#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"

static double now_sec(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}

static unsigned char* load(const char* path, long* szOut){
    FILE* f=fopen(path,"rb"); if(!f){perror("fopen");exit(1);}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char* d=malloc(sz);
    if(fread(d,1,sz,f)!=(size_t)sz){perror("fread");exit(1);}
    fclose(f); *szOut=sz; return d;
}

int main(int argc,char**argv){
    if(argc<4){fprintf(stderr,"usage: %s <in.flac> <serial|Nthreads> <iters> [out.raw]\n",argv[0]);return 1;}
    const char* path=argv[1]; int mode=atoi(argv[2]); int iters=atoi(argv[3]);
    const char* outpath=argc>4?argv[4]:NULL;
    long sz; unsigned char* data=load(path,&sz);

    drflac* probe=drflac_open_memory(data,sz,NULL);
    drflac_uint64 total=probe->totalPCMFrameCount;
    drflac_uint32 ch=probe->channels, sr=probe->sampleRate;
    drflac_close(probe);

    drflac_int32* out=malloc((size_t)total*ch*sizeof(drflac_int32));
    double best=1e9,sum=0; drflac_uint64 got=0;
    for(int it=0;it<iters;it++){
        double t0=now_sec();
        if(mode==0){
            drflac* dec=drflac_open_memory(data,sz,NULL);
            got=drflac_read_pcm_frames_s32(dec,total,out);
            drflac_close(dec);
        }else{
            unsigned int co,so; drflac_uint64 to;
            got=drflac_decode_memory_pcm_frames_s32_parallel(data,sz,mode,out,&co,&so,&to);
        }
        double t1=now_sec(); double dt=t1-t0; if(dt<best)best=dt; sum+=dt;
        if(got!=total){fprintf(stderr,"decode fail\n");return 2;}
    }
    if(outpath){FILE*o=fopen(outpath,"wb");fwrite(out,4,(size_t)total*ch,o);fclose(o);}
    double dur=(double)total/sr;
    printf("%s mode=%s dur=%.3fs best=%.4fs avg=%.4fs speed=%.2fx\n",
        path, mode==0?"serial":argv[2], dur, best, sum/iters, dur/best);
    free(out); free(data); return 0;
}
