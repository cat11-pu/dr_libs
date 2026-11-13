#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DRFLAC_USE_PTHREAD
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n)exit(2);fclose(f);return d;}
int main(int c,char**v){
    long n;unsigned char*d=load(v[1],&n);
    drflac* probe=drflac_open_memory(d,n,NULL);
    drflac_uint64 total=probe->totalPCMFrameCount;drflac_uint32 ch=probe->channels;drflac_close(probe);
    printf("total=%llu ch=%u\n",(unsigned long long)total,ch);
    drflac_int32* par=calloc(total*ch,4);
    unsigned int co,so;drflac_uint64 to;
    drflac_uint64 r=drflac_decode_memory_pcm_frames_s32_parallel(d,n,2,par,&co,&so,&to);
    printf("parallel ret=%llu (total=%llu) co=%u so=%u to=%llu\n",(unsigned long long)r,(unsigned long long)total,co,so,(unsigned long long)to);
    drflac_int32* ser=malloc(total*ch*4);
    drflac* sd=drflac_open_memory(d,n,NULL);drflac_uint64 ns=drflac_read_pcm_frames_s32(sd,total,ser);drflac_close(sd);
    printf("serial frames=%llu\n",(unsigned long long)ns);
    size_t firstdiff=(size_t)-1;
    for(size_t i=0;i<(size_t)ns*ch;i++) if(ser[i]!=par[i]){firstdiff=i;break;}
    printf("first diff sample index=%zu (frame=%zu)\n",firstdiff, firstdiff/ch);
    free(par);free(ser);free(d);return 0;
}
