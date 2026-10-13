/* Compare entire VRAM after mixed clipped, textured and blended primitives.
 * Both backends start with identical data, including texture/CLUT feedback. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "gp0.h"

static uint16_t actual[1024*512], expected[1024*512];
uint16_t *psx_vram;
Gp0State gp0_state;
const Gp0Backend *gp0_backend;
extern const Gp0Backend psx_softgpu, psx_softgpu_reference;
static uint32_t seed = 0x17932761;
static unsigned random_value(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static void draw(const Gp0Backend *backend, const Gp0Mode *m, const Gp0Vtx *v, int type, int w, int h) {
    if (type < 2) backend->poly(m, v, type+3);
    else if (type == 2) backend->rect(m, v, w, h);
    else backend->line(m, v, v+1);
}
static void targeted_checks(void) {
    /* A screen-aligned quad must cover the same top/left-inclusive,
     * bottom/right-exclusive pixels as a rectangle, including row zero. */
    for(int blend_mode=0;blend_mode<5;++blend_mode) {
        for(unsigned i=0;i<1024*512;++i) actual[i]=expected[i]=0x1234;
        Gp0Mode m={.semi=blend_mode<4,.abr=blend_mode%4};
        Gp0Vtx v[4]={{0,0,191,97,21,0,0},{96,0,191,97,21,0,0},
                     {0,96,191,97,21,0,0},{96,96,191,97,21,0,0}};
        gp0_state=(Gp0State){.area_x1=1023,.area_y1=511};
        psx_vram=actual;psx_softgpu.poly(&m,v,4);
        psx_vram=expected;psx_softgpu_reference.rect(&m,v,96,96);
        assert(memcmp(actual,expected,sizeof actual)==0);
        assert(actual[96]==0x1234 && actual[96*1024]==0x1234);
    }
    for (int depth=0; depth<2; ++depth) for (int abr=0; abr<4; ++abr)
    for (int feedback=0; feedback<2; ++feedback) for (int colour=0; colour<2; ++colour) {
        for (unsigned i=0; i<1024*512; ++i) actual[i]=0x1234;
        Gp0Mode m={.textured=1,.semi=1,.depth=depth,.abr=abr,.tx=640,.ty=256,
                   .clut_x=1008,.clut_y=feedback ? 100 : 450};
        int count=depth ? 256 : 16;
        for (int i=0;i<count;++i)
            actual[m.clut_y*1024+((m.clut_x+i)&1023)]=(i%4==0) ? 0 : (i%4==1) ? 0x8000 : (i%4==2) ? 0xFFFF : 0x7FFF;
        Gp0Vtx v={.x=0,.y=100,.r=colour ? 255 : 0,.g=colour ? 191 : 0,
                  .b=colour ? 127 : 0,.u=240,.v=240};
        gp0_state=(Gp0State){.area_x1=1023,.area_y1=511};
        memcpy(expected,actual,sizeof actual);
        psx_vram=actual; psx_softgpu.rect(&m,&v,128,80);
        psx_vram=expected; psx_softgpu_reference.rect(&m,&v,128,80);
        assert(memcmp(actual,expected,sizeof actual)==0);
    }
}
static void shadow_checks(void) {
    extern void psx_softgpu_shadow_poly(const Gp0Mode *, const Gp0Vtx *, int,
                                       uint16_t *, uint8_t *, int, int, int, int);
    extern void psx_softgpu_reference_shadow_poly(const Gp0Mode *, const Gp0Vtx *, int,
                                       uint16_t *, uint8_t *, int, int, int, int);
    uint16_t pixels[2][64*64];
    uint8_t known[2][64*64];
    for (int depth=0;depth<2;++depth) for (int semi=0;semi<2;++semi)
    for(int mask=0;mask<4;++mask) {
        Gp0Mode m={.textured=1,.semi=semi,.depth=depth,.tx=640,.ty=256,
                   .clut_x=0,.clut_y=10};
        Gp0Vtx v[4]={{0,0,255,97,21,0,0},{64,0,255,97,21,64,0},
                     {0,64,255,97,21,0,64},{64,64,0,0,0,64,64}};
        for(unsigned i=0;i<64*64;++i) {pixels[0][i]=random_value();known[0][i]=i%2;}
        memcpy(pixels[1],pixels[0],sizeof pixels[0]);
        memcpy(known[1],known[0],sizeof known[0]);
        gp0_state=(Gp0State){.area_x1=1023,.area_y1=511,.check_mask=mask&1,.set_mask=mask>>1};
        psx_vram=actual;
        psx_softgpu_shadow_poly(&m,v,4,pixels[0],known[0],0,0,64,64);
        psx_softgpu_reference_shadow_poly(&m,v,4,pixels[1],known[1],0,0,64,64);
        assert(memcmp(pixels[0],pixels[1],sizeof pixels[0])==0);
        assert(memcmp(known[0],known[1],sizeof known[0])==0);
    }
}
static void benchmark(void) {
    Gp0Mode m={.textured=1,.depth=1,.tx=640,.ty=256,.clut_x=512,.clut_y=450};
    Gp0Vtx v[4]={{0,0,97,191,255,0,0},{96,0,97,191,255,96,0},
                 {0,96,97,191,255,0,96},{96,96,97,191,255,96,96}};
    gp0_state=(Gp0State){.area_x1=1023,.area_y1=511};
    for(unsigned i=0;i<1024*512;++i) actual[i]=random_value();
    memcpy(expected,actual,sizeof actual);
    const Gp0Backend *backends[2]={&psx_softgpu_reference,&psx_softgpu};
    double seconds[2];
    for (int b=0;b<2;++b) {
        psx_vram=b ? actual : expected;
        clock_t start=clock();
        for(int i=0;i<4000;++i) backends[b]->poly(&m,v,4);
        seconds[b]=(double)(clock()-start)/CLOCKS_PER_SEC;
    }
    assert(memcmp(actual,expected,sizeof actual)==0);
    printf("flat indexed quad CPU benchmark: generic %.3fs, optimised %.3fs (%.2fx); host only\n",
           seconds[0],seconds[1],seconds[0]/seconds[1]);
}
int main(int argc, char **argv) {
    (void)argv;
    targeted_checks();
    shadow_checks();
    for (unsigned i=0; i<1024*512; ++i)
        actual[i] = (i % 17 == 0) ? 0 : (i % 19 == 0) ? 0x8000 : random_value();
    memcpy(expected, actual, sizeof actual);
    for (unsigned trial=0; trial<4096; ++trial) {
        Gp0Mode m = {.textured=random_value()%2, .raw=random_value()%2,
                    .semi=random_value()%2, .gouraud=random_value()%2,
                    .abr=random_value()%4, .depth=random_value()%3,
                    .tx=(random_value()%16)*64, .ty=(random_value()%2)*256,
                    .clut_x=(random_value()%64)*16, .clut_y=random_value()%512};
        int x=random_value()%1056-16, y=random_value()%544-16;
        Gp0Vtx v[4];
        for (int i=0; i<4; ++i)
            v[i]=(Gp0Vtx){x+(int)(random_value()%90),y+(int)(random_value()%90),
                          random_value()%256,random_value()%256,random_value()%256,
                          random_value()%256,random_value()%256};
        /* Flat quads deliberately have different vertex colours: triangle 2
         * must use vertex 1, rather than a cached colour from vertex 0. */
        gp0_state=(Gp0State){.area_x0=0,.area_y0=0,.area_x1=1023,.area_y1=511,
            .twin_mask_x=(random_value()%32)*8,.twin_mask_y=(random_value()%32)*8,
            .twin_off_x=(random_value()%32)*8,.twin_off_y=(random_value()%32)*8,
            .check_mask=random_value()%2,.set_mask=random_value()%2};
        if (trial%4 == 0) {
            gp0_state.area_x0=x+20; gp0_state.area_y0=y+20;
            gp0_state.area_x1=x+65; gp0_state.area_y1=y+65;
        }
        unsigned type=trial%4;
        int w=random_value()%100, h=random_value()%100;
        psx_vram=actual; draw(&psx_softgpu,&m,v,type,w,h);
        psx_vram=expected; draw(&psx_softgpu_reference,&m,v,type,w,h);
        if (memcmp(actual,expected,sizeof actual)) {
            for (unsigned i=0;i<1024*512;++i) if(actual[i]!=expected[i]) {
                fprintf(stderr,"trial %u type %u flat %d pixel %u,%u: %04x != %04x\n",
                        trial,type,!m.gouraud,i%1024,i/1024,actual[i],expected[i]);
                return 1;
            }
        }
    }
    puts("software raster: 4096 mixed primitives match the generic path byte-for-byte");
    if (argc > 1) benchmark();
    return 0;
}
