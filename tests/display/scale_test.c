#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "display_scale.h"

static uint16_t vram[1024*512], output[256*192+1], full[256*192+1];
int main(void) {
    /* A one-pixel white stroke discarded by nearest-neighbour survives with
     * its actual coverage; adjacent green must not carry into red or blue. */
    for (int y=0; y<512; ++y) {
        vram[y*1024+1] = 0xffff;
        vram[y*1024+2] = 31 << 5;
    }
    ds_scale_display(output,vram,0,0,320,224);
    assert(output[0] == (0x8000 | 6 | (6<<5) | (6<<10)));
    assert(output[1] == (0x8000 | 19 | (31<<5) | (19<<10)));
    /* Draw-buffer glyphs must not replace coverage for the displayed buffer. */
    DsTextRect current={320,0,3,13},previous={0,0,3,13};
    ds_scale_display_text_pair(output,vram,0,0,320,224,&current,1,&previous,1);
    assert(output[0] == (0x8000 | 6 | (6<<5) | (6<<10)));
    assert(output[1] == (0x8000 | 19 | (31<<5) | (19<<10)));
    for (unsigned i=0;i<1024*512;++i) vram[i]=(i*173+19) & 0xffff;
    const int sizes[][2]={{320,224},{320,218},{320,240},{256,192},{3,3},{512,256}};
    for(unsigned t=0;t<sizeof sizes/sizeof sizes[0];++t) {
        int w=sizes[t][0]>320 ? 320 : sizes[t][0];
        int h=sizes[t][1]>240 ? 240 : sizes[t][1];
        memset(output,0x5a,sizeof output);
        ds_scale_display(output,vram,1019,509,sizes[t][0],sizes[t][1]);
        for(int oy=0;oy<h*4/5;++oy) for(int ox=0;ox<w*4/5;++ox) {
            unsigned expected=0x8000;
            /* Independent scalar channel sums, with both axes wrapping. */
            for(int channel=0;channel<3;++channel) {
                unsigned sum=0;
                for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
                    int sx=ox*5/4+i,sy=oy*5/4+j;
                    if(sx>=w) sx=w-1;
                    if(sy>=h) sy=h-1;
                    unsigned weight_x=i ? (ox%4)+1 : 4-(ox%4);
                    unsigned weight_y=j ? (oy%4)+1 : 4-(oy%4);
                    unsigned pixel=vram[((509+sy)&511)*1024+((1019+sx)&1023)];
                    sum+=((pixel>>(channel*5))&31)*weight_x*weight_y;
                }
                expected|=((sum+12)/25)<<(channel*5);
            }
            assert(output[oy*256+ox]==expected);
        }
        assert(output[(h*4/5)*256]==0x5a5a);
        memcpy(full,output,sizeof output);
        DsTextRect all={1019,509,w,h};
        ds_scale_display_text(output,vram,1019,509,w,h,&all,1);
        assert(memcmp(output,full,sizeof output)==0);
        ds_scale_display_text(output,vram,1019,509,w,h,NULL,0);
        for(int oy=0;oy<h*4/5;++oy) for(int ox=0;ox<w*4/5;++ox)
            assert(output[oy*256+ox]==(vram[((509+oy*5/4)&511)*1024+((1019+ox*5/4)&1023)]|0x8000));
    }
    memset(output,0x5a,sizeof output);
    DsTextRect border[]={{-5,-5,8,13},{320,100,10,13},{0,511,10,13}};
    ds_scale_display_text(output,vram,0,0,320,224,border,3);
    assert(output[179*256]==0x5a5a);
    puts("DS area scale: strokes, channels, wrapping and cropped borders pass");
}
