#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DRFLAC_USE_PTHREAD
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n){fclose(f);return NULL;}fclose(f);return d;}
int main(int c,char**v){
    int fails=0;
    for(int a=1;a<c;a++){
        long n;unsigned char*d=load(v[a],&n);
        drflac*probe=drflac_open_memory(d,n,NULL);
        drflac_uint64 total=probe->totalPCMFrameCount;drflac_uint32 ch=probe->channels;drflac_close(probe);
        drflac_int32*ser=malloc((size_t)total*ch*4);
        drflac*sd=drflac_open_memory(d,n,NULL);drflac_uint64 ns=drflac_read_pcm_frames_s32(sd,total,ser);drflac_close(sd);
        int okall=1;
        for(int nt=2;nt<=8;nt+=2){
            drflac_int32*par=malloc((size_t)total*ch*4);
            unsigned int co,so;drflac_uint64 to;
            drflac_uint64 np=drflac_decode_memory_pcm_frames_s32_parallel(d,n,nt,par,&co,&so,&to);
            int ok=(np==ns)&&(memcmp(ser,par,(size_t)ns*ch*4)==0);
            if(!ok){okall=0;printf("  MISMATCH %s threads=%d\n",v[a],nt);}
            free(par);
        }
        printf("%s frames=%llu ch=%u %s\n",v[a],(unsigned long long)ns,ch,okall?"PARALLEL-EXACT(t2..t8)":"FAIL");
        if(!okall)fails++;
        free(ser);free(d);
    }
    printf(fails?"PAR FAIL=%d\n":"ALL PARALLEL DECODES SAMPLE-EXACT\n",fails);
    return fails?1:0;
}
