#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define DR_FLAC_IMPLEMENTATION
#include HDR
static double now_sec(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n)exit(2);fclose(f);return d;}
int main(int c,char**v){
    int iters=atoi(v[3]); long n;unsigned char*d=load(v[1],&n);
    drflac*probe=drflac_open_memory(d,n,NULL);drflac_uint64 total=probe->totalPCMFrameCount;drflac_uint32 ch=probe->channels,sr=probe->sampleRate;drflac_close(probe);
    drflac_int32* out=malloc((size_t)total*ch*4);double best=1e9,sum=0;
    for(int it=0;it<iters;it++){drflac*dec=drflac_open_memory(d,n,NULL);double t0=now_sec();drflac_uint64 g=drflac_read_pcm_frames_s32(dec,total,out);double t1=now_sec();(void)g;drflac_close(dec);double dt=t1-t0;if(dt<best)best=dt;sum+=dt;}
    if(v[4]){FILE*o=fopen(v[4],"wb");fwrite(out,4,(size_t)total*ch,o);fclose(o);}
    printf("%s serial best=%.4fs avg=%.4fs speed=%.2fx\n",v[2],best,sum/iters,(double)total/sr/best);
    return 0;
}
