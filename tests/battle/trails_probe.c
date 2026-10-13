#include "psyq_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
unsigned int port_effect_data(unsigned int address) {
    return address >= 0x80000000 && address < 0x80200000
        ? (unsigned int)PSX_ADDR(address) : address;
}
s32 o0b_func_800CF5A4(u8 *, s32, s32, s32);
int main(void) {
    if (mmap(psx_ram, PSX_RAM_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != psx_ram) abort();
    u32 args[3];
    while (fread(args, sizeof args, 1, stdin) == 1) {
        if (fread(psx_ram + 0x180000, 512, 1, stdin) != 1 ||
            fread(psx_ram + 0x181000, 8, 1, stdin) != 1) abort();
        s32 position = args[2] ? 0x80181000 : (s32)(psx_ram + 0x181000);
        u32 result = o0b_func_800CF5A4(psx_ram + 0x180000, args[0], args[1], position);
        fwrite(&result, 4, 1, stdout);
        fwrite(psx_ram + 0x180000, 512, 1, stdout);
    }
}
