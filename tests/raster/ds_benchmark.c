/* Disc-free ARM9 timing probe for the production software rasteriser. */
#include <nds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gp0.h"

static u16 initial[1024*512], pixels[1024*512], reference[1024*512];
u16 *psx_vram=pixels;
Gp0State gp0_state;
const Gp0Backend *gp0_backend;
extern const Gp0Backend psx_softgpu, psx_softgpu_reference, psx_softgpu_arm_itcm;
static unsigned seed=0x38412;
static unsigned random_value(void) {
    seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed;
}
int main(void) {
    consoleDemoInit();
    printf("ARM9 software raster timing\n");
    for(unsigned i=0;i<1024*512;++i) initial[i]=random_value();
    const Gp0Backend *backends[]={&psx_softgpu_reference,&psx_softgpu,&psx_softgpu_arm_itcm};
    const char *names[]={"flat quad", "Gouraud quad", "subtract quad", "indexed rectangle", "solid Gouraud subtract", "solid rectangle", "solid flat quad"};
    printf("Generic / fast Thumb / ARM ITCM\n");
    for(int test=0;test<7;++test) {
        Gp0Mode m={.textured=test<4,.depth=test==3 ? 0 : 1,.tx=640,.ty=256,
                   .clut_x=512,.clut_y=450,.gouraud=test==1 || test==4,.semi=test==2 || test==4,.abr=2};
        Gp0Vtx v[4]={{0,0,97,191,255,0,0},{96,0,153,67,21,96,0},
                     {0,96,255,97,191,0,96},{96,96,21,255,97,96,96}};
        gp0_state=(Gp0State){.area_x1=1023,.area_y1=511};
        printf("\n%s\n",names[test]);
        for(int b=0;b<3;++b) {
            memcpy(pixels,initial,sizeof pixels);
            cpuStartTiming(0);
            for(int i=0;i<64;++i) {
                if(test==3 || test==5) backends[b]->rect(&m,v,96,96);
                else backends[b]->poly(&m,v,4);
            }
            unsigned ticks=cpuEndTiming();
            if(b==0) memcpy(reference,pixels,sizeof pixels);
            else assert(memcmp(reference,pixels,sizeof pixels)==0);
            printf("%lu%s",(unsigned long)ticks,b==2 ? "\n" : " ");
        }
    }
    printf("\nPixels match in all cases.\n");
    while(1) swiWaitForVBlank();
}
