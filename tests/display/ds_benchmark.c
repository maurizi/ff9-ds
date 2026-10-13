#include <nds.h>
#include <stdio.h>
#include "display_scale.h"

static uint16_t vram[1024*512], output[256*192];
int main(void) {
    consoleDemoInit();
    for(unsigned i=0;i<1024*512;++i) vram[i]=(i*173+19)&0xffff;
    const int heights[]={224,218,240};
    printf("ARM9 4/5 area scaling\n64 full display frames\n");
    for(int i=0;i<3;++i) {
        cpuStartTiming(0);
        for(int j=0;j<64;++j) ds_scale_display(output,vram,320,0,320,heights[i]);
        unsigned ticks=cpuEndTiming();
        printf("320x%d: %lu ticks\n",heights[i],(unsigned long)ticks);
        printf("per frame: %lu ticks\n",(unsigned long)(ticks/64));
    }
    DsTextRect rects[100];
    for(int i=0;i<100;++i) rects[i]=(DsTextRect){320+(i%25)*10,32+(i/25)*15,7,13};
    for(int count=0;count<=100;count+=100) {
        cpuStartTiming(0);
        for(int j=0;j<64;++j) ds_scale_display_text(output,vram,320,0,320,224,rects,count);
        unsigned ticks=cpuEndTiming();
        printf("%d glyphs: %lu/frame\n",count,(unsigned long)(ticks/64));
    }
    printf("\nDone.\n");
    while(1) swiWaitForVBlank();
}
