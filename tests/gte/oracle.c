/* Shared production GTE for the original-MIPS CPU differential harness. */
#include "../../port/psx/gte.c"
void oracle_set(const void *state) { __builtin_memcpy(&g,state,sizeof g); }
void oracle_get(void *state) { __builtin_memcpy(state,&g,sizeof g); }
