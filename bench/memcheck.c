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
    drflac_int32* out=malloc((size_t)total*ch*4);
    if(atoi(v[2])==0){drflac*dec=drflac_open_memory(d,n,NULL);drflac_read_pcm_frames_s32(dec,total,out);drflac_close(dec);}
    else{unsigned a,b;drflac_uint64 x;drflac_decode_memory_pcm_frames_s32_parallel(d,n,atoi(v[2]),out,&a,&b,&x);}
    /* hold everything, report buffer size */
    fprintf(stderr,"output buffer=%zu bytes input=%ld bytes\n",(size_t)total*ch*4,n);
    free(out);free(d);return 0;
}
