#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
static unsigned char* load(const char* p,long* n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n){fclose(f);free(d);return NULL;}fclose(f);return d;}
int main(int argc,char**argv){
    int fails=0;
    for(int a=1;a<argc;a++){
        long sz; unsigned char* d=load(argv[a],&sz); if(!d){printf("OPENFAIL %s\n",argv[a]);fails++;continue;}
        drflac* dec=drflac_open_memory(d,sz,NULL);
        if(!dec){printf("BADFLAC %s\n",argv[a]);free(d);fails++;continue;}
        size_t cap=(size_t)(dec->totalPCMFrameCount+16)*dec->channels;
        drflac_int32* ref=malloc(cap*4);
        drflac_uint64 n=drflac_read_pcm_frames_s32(dec,dec->totalPCMFrameCount,ref);
        drflac_close(dec);
        /* re-decode and compare every sample */
        drflac* dec2=drflac_open_memory(d,sz,NULL);
        drflac_int32* chk=malloc(cap*4);
        drflac_uint64 n2=drflac_read_pcm_frames_s32(dec2,dec2->totalPCMFrameCount,chk);
        drflac_close(dec2);
        int ok=(n==n2)&&(memcmp(ref,chk,(size_t)n*dec->channels*4)==0);
        printf("%s ch=%u bps=%u frames=%llu %s\n",argv[a],dec->channels,dec->bitsPerSample,(unsigned long long)n,ok?"OK":"MISMATCH");
        if(!ok)fails++;
        free(ref);free(chk);free(d);
    }
    printf(fails?"CORPUS FAILURES=%d\n":"ALL CORPUS SAMPLE-EXACT\n",fails);
    return fails?1:0;
}
