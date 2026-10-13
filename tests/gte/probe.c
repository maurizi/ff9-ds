/* Run the production GTE on raw register snapshots. Direct raw access models a
 * debugger snapshot, rather than normalising fixtures through mtc2/ctc2. */
#include <stdio.h>
#include "../../port/psx/gte.c"
int main(void) {
    u32 op;
    while (fread(&op, sizeof op, 1, stdin) == 1) {
        if (fread(&g, sizeof g, 1, stdin) != 1) return 2;
        gte_cmd(op);
        if (fwrite(&g, sizeof g, 1, stdout) != 1) return 2;
    }
    return 0;
}
