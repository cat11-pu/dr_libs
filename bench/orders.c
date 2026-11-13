#include <stdio.h>
#include <stdlib.h>
#define DR_FLAC_IMPLEMENTATION
#include "../dr_flac.h"
int main(int argc, char** argv){
    FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char* d=malloc(sz); if(fread(d,1,sz,f)!=(size_t)sz) return 3; fclose(f);
    drflac* dec=drflac_open_memory(d,sz,NULL);
    drflac_int32* buf=malloc((size_t)(dec->maxBlockSizeInPCMFrames+16)*dec->channels*4);
    long typeCount[256]={0}; long orderCount[33]={0};
    drflac_uint64 got;
    while((got=drflac_read_pcm_frames_s32(dec, dec->maxBlockSizeInPCMFrames, buf))>0){
        for(int c=0;c<dec->channels;c++){
            unsigned t=dec->currentFLACFrame.subframes[c].subframeType;
            typeCount[t]++;
            if(t==DRFLAC_SUBFRAME_LPC) orderCount[dec->currentFLACFrame.subframes[c].lpcOrder]++;
            if(t==DRFLAC_SUBFRAME_FIXED) orderCount[dec->currentFLACFrame.subframes[c].lpcOrder]++;
        }
    }
    printf("%s\n constant=%ld verbatim=%ld fixed=%ld lpc=%ld\n", argv[1],
        typeCount[DRFLAC_SUBFRAME_CONSTANT], typeCount[DRFLAC_SUBFRAME_VERBATIM],
        typeCount[DRFLAC_SUBFRAME_FIXED], typeCount[DRFLAC_SUBFRAME_LPC]);
    printf(" orders:");
    for(int i=0;i<=32;i++) if(orderCount[i]) printf(" %d:%ld", i, orderCount[i]);
    printf("\n");
    drflac_close(dec); free(buf); free(d); return 0;
}
