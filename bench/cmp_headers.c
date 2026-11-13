#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DR_FLAC_IMPLEMENTATION
#include HDR
static unsigned char* load(const char*p,long*n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*d=malloc(*n);if(fread(d,1,*n,f)!=(size_t)*n)exit(2);fclose(f);return d;}
int main(int c,char**v){
    int fails=0;
    for(int a=1;a<c;a++){long n;unsigned char*d=load(v[a],&n);drflac*dec=drflac_open_memory(d,n,NULL);
        size_t cap=(size_t)(dec->totalPCMFrameCount+16)*dec->channels;drflac_int32*s=malloc(cap*4);
        drflac_uint64 got=drflac_read_pcm_frames_s32(dec,dec->totalPCMFrameCount,s);drflac_close(dec);
        char out[512];snprintf(out,sizeof out,"%s/%s.raw",OUTDIR,strrchr(v[a],'/')+1);
        FILE*o=fopen(out,"wb");fwrite(s,4,(size_t)got*dec->channels,o);fclose(o);free(s);free(d);}
    return 0;
}
