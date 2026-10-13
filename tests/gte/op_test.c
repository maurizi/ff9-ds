#include <assert.h>
#include <stdint.h>
#include "gte.h"
static void inputs(int x, int y, int z, int a, int b, int c) {
    for (int i=0; i<32; i++) { gte_mtc2(i,0); gte_ctc2(i,0); }
    gte_mtc2(9,(uint32_t)x); gte_mtc2(10,(uint32_t)y); gte_mtc2(11,(uint32_t)z);
    gte_ctc2(0,(uint16_t)a); gte_ctc2(2,(uint16_t)b); gte_ctc2(4,(uint16_t)c);
}
static void result(int x, int y, int z, int ix, int iy, int iz, uint32_t flag) {
    int mac[3]={x,y,z}, ir[3]={ix,iy,iz};
    for(int i=0;i<3;i++) {
        assert((int32_t)gte_mfc2(25+i)==mac[i]);
        assert((int32_t)gte_mfc2(9+i)==ir[i]);
    }
    assert(gte_cfc2(31)==flag);
}
int main(void) {
    /* Unequal inputs make every in-place overwrite visible. */
    inputs(7,11,13,2,3,5); gte_cmd(0x0C);
    result(-16,9,1,-16,9,1,0);
    inputs(7,11,13,2,3,5); gte_cmd(0x40C);
    result(-16,9,1,0,9,1,0x81000000);
    inputs(4096,0,0,0,4096,0); gte_cmd(0x8000C);
    result(0,0,-4096,0,0,-4096,0);
    inputs(30000,-30000,30000,30000,30000,-30000); gte_cmd(0x0C);
    result(0,-1800000000,-1800000000,0,-32768,-32768,0x80C00000);
    inputs(30000,-30000,30000,30000,30000,-30000); gte_cmd(0x8000C);
    result(0,-439454,-439454,0,-32768,-32768,0x80C00000);
    return 0;
}
