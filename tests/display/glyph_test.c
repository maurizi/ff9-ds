#include <assert.h>
#include <stdio.h>
#include "gp0.h"
static int glyphs, drawings, last[4];
void psx_display_drawing(void) { drawings++; }
void psx_display_glyph(int x,int y,int w,int h) {
    glyphs++;last[0]=x;last[1]=y;last[2]=w;last[3]=h;
}
static void rect(const Gp0Mode *m,const Gp0Vtx *v,int w,int h) {}
static const Gp0Backend backend={.rect=rect};
static void sprite_uv(unsigned page,int x,int y,int w,int h,unsigned uv) {
    unsigned commands[]={0xe1000000|page,0x64808080,(y<<16)|(x&0xffff),uv,(h<<16)|w};
    gp0_run(commands,5);
}
static void sprite(unsigned page,int x,int y,int w,int h) { sprite_uv(page,x,y,w,h,0); }
int main(void) {
    gp0_backend=&backend;
    gp0_state=(Gp0State){.area_x0=10,.area_y0=20,.area_x1=100,.area_y1=80};
    sprite(0x61e,8,18,7,13);
    assert(glyphs==1 && last[0]==10 && last[1]==20 && last[2]==5 && last[3]==11);
    sprite(0x61f,50,50,8,8); assert(glyphs==2);
    sprite(0x61e,50,50,32,32); assert(glyphs==2);
    sprite(0x61e,120,50,7,13); assert(glyphs==2);
    sprite(0x69e,50,50,7,13); assert(glyphs==2);
    sprite(0x61e,50,50,7,13); assert(glyphs==3);
    sprite(0x1f,50,50,7,13); assert(glyphs==4);
    assert(drawings==7);
    sprite_uv(0x61e,50,50,84,12,240<<8);assert(glyphs==5 && last[2]==51);
    sprite_uv(0x61e,50,50,84,12,(239<<8));assert(glyphs==5);
    sprite_uv(0x71f,50,50,29,10,176<<8);assert(glyphs==6);
    sprite_uv(0x71f,50,50,22,10,(212<<8)|32);assert(glyphs==7);
    sprite_uv(0x71f,50,50,22,10,(213<<8)|32);assert(glyphs==7);
    sprite_uv(0x71f,50,50,22,10,(212<<8)|31);assert(glyphs==7);
    sprite_uv(0x61f,50,50,8,10,(64<<8)|152);assert(glyphs==8);
    sprite_uv(0x61f,50,50,8,10,(64<<8)|153);assert(glyphs==8);
    puts("GP0 font coverage: pages, sizes and drawing-area clipping pass");
}
