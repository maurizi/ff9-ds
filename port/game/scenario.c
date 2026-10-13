/* Declarative test launch through normal loaders. No CPU state, stack,
 * executable bytes or runtime pointers are imported. */
#include "psyq_port.h"
#define printf port_psx_printf_declaration
#include "ff9.h"
#undef printf
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
void psx_cd_select_disc(unsigned);
void o14_func_800A7578(void);
void o14_func_800A741C(void);
s32 func_80033C48(s32,s32,u8);
static struct {
    int enabled,consumed,kind,disc,field,battle,level,story;
    unsigned seed;
    int party[4],hp[4],equip[4][5],events[0x800],items[256];
    char characters[256],event_blob[256];
} scenario;
static int number(const char *s,int low,int high) {
    char *end;errno=0;long n=strtol(s,&end,0);
    if(errno==ERANGE || end==s || *end || n<low || n>high) port_fatal("scenario: invalid value %s",s);
    return (int)n;
}
void port_scenario_load(const char *path) {
    FILE *f=fopen(path,"r");
    if(!f) port_fatal("scenario: cannot open %s",path);
    __builtin_memset(&scenario,0,sizeof scenario);
    scenario.disc=1;scenario.field=70;scenario.battle=-1;
    scenario.level=-1;scenario.story=-1;scenario.seed=1;
    for(int i=0;i<4;++i) {scenario.party[i]=i;scenario.hp[i]=-1;for(int j=0;j<5;++j) scenario.equip[i][j]=-1;}
    for(int i=0;i<0x800;++i) scenario.events[i]=-1;
    for(int i=0;i<256;++i) scenario.items[i]=-1;
    char line[512];
    while(fgets(line,sizeof line,f)) {
        if(!__builtin_strchr(line,'\n') && !feof(f)) port_fatal("scenario: line too long");
        char *s=line;while(*s==' ' || *s=='\t') ++s;
        if(!*s || *s=='#' || *s=='\n') continue;
        char *eq=__builtin_strchr(s,'=');if(!eq) port_fatal("scenario: expected key=value");*eq++=0;
        char *key_end=eq-1;while(key_end>s && (key_end[-1]==' ' || key_end[-1]=='\t')) *--key_end=0;
        while(*eq==' ' || *eq=='\t') ++eq;
        char *end=eq+__builtin_strlen(eq);while(end>eq && (end[-1]=='\n' || end[-1]=='\r' || end[-1]==' ' || end[-1]=='\t')) *--end=0;
        if(!__builtin_strcmp(s,"kind")) {
            if(!__builtin_strcmp(eq,"field")) scenario.kind=1;
            else if(!__builtin_strcmp(eq,"battle")) scenario.kind=2;
            else port_fatal("scenario: kind must be field or battle");
        } else if(!__builtin_strcmp(s,"disc")) scenario.disc=number(eq,1,4);
        else if(!__builtin_strcmp(s,"field")) scenario.field=number(eq,0,32767);
        else if(!__builtin_strcmp(s,"battle")) scenario.battle=number(eq,0,65535);
        else if(!__builtin_strcmp(s,"level")) scenario.level=number(eq,1,99);
        else if(!__builtin_strcmp(s,"story")) scenario.story=number(eq,0,65535);
        else if(!__builtin_strcmp(s,"seed")) scenario.seed=(unsigned)number(eq,0,0x7fffffff);
        else if(!__builtin_strcmp(s,"party")) {
            int used=0;
            if(sscanf(eq,"%d,%d,%d,%d%n",scenario.party,scenario.party+1,scenario.party+2,scenario.party+3,&used)!=4 || eq[used]) port_fatal("scenario: party needs four indices");
            for(int i=0;i<4;++i) {
                if(scenario.party[i]<-1 || scenario.party[i]>8) port_fatal("scenario: party outside -1..8");
                for(int j=0;j<i;++j) if(scenario.party[i]>=0 && scenario.party[i]==scenario.party[j]) port_fatal("scenario: duplicate party member");
            }
            if(scenario.party[0]<0) port_fatal("scenario: first party slot must be occupied");
        } else if(!__builtin_strcmp(s,"characters") || !__builtin_strcmp(s,"events")) {
            char *dst=!__builtin_strcmp(s,"characters")?scenario.characters:scenario.event_blob;
            if(__builtin_strlen(eq)>=256) port_fatal("scenario: blob path too long");
            __builtin_strcpy(dst,eq);
        } else if(!__builtin_strncmp(s,"hp.",3)) scenario.hp[number(s+3,0,3)]=number(eq,0,9999);
        else if(!__builtin_strncmp(s,"event.",6)) scenario.events[number(s+6,0,0x7ff)]=number(eq,0,255);
        else if(!__builtin_strncmp(s,"item.",5)) scenario.items[number(s+5,0,255)]=number(eq,0,99);
        else if(!__builtin_strncmp(s,"equip.",6)) {
            int slot=number(s+6,0,3),used=0,*v=scenario.equip[slot];
            if(sscanf(eq,"%d,%d,%d,%d,%d%n",v,v+1,v+2,v+3,v+4,&used)!=5 || eq[used]) port_fatal("scenario: equipment needs five IDs");
            for(int i=0;i<5;++i) if(v[i]<0 || v[i]>255) port_fatal("scenario: equipment outside 0..255");
        } else port_fatal("scenario: unknown key %s",s);
    }
    if(ferror(f)) port_fatal("scenario: error reading %s",path);
    fclose(f);
    if(!scenario.kind || (scenario.kind==2 && scenario.battle<0)) port_fatal("scenario: kind and battle ID required");
    scenario.enabled=1;psx_cd_select_disc((unsigned)scenario.disc);
    port_log("scenario configured: disc %d, %s %d\n",scenario.disc,scenario.kind==1?"field":"battle",scenario.kind==1?scenario.field:scenario.battle);
}
static void load_blob(const char *path,void *data,size_t size) {
    if(!*path) return;
    FILE *f=fopen(path,"rb");
    if(!f || fread(data,1,size,f)!=size || fgetc(f)!=EOF) port_fatal("scenario: blob %s must be %u bytes",path,(unsigned)size);
    fclose(f);
}
/* Field/battle initialization normally reseeds from a hardware counter. */
s32 port_scenario_seed(s32 ordinary_seed) {
    return scenario.enabled ? (s32)scenario.seed : ordinary_seed;
}
static void prepare_battle_resources(GameData *d) {
    /* The ordinary field/world transition supplies these two CD ranges.
     * Resolve the encounter's bundle and its background dependency directly
     * from the selected disc's own directories instead of fabricating them. */
    FileDirEntry *dirs=(FileDirEntry *)g_gameState->unk14;
    FileIdEntry *entries=(FileIdEntry *)(D_80067944+0x14c700+(dirs[5].unk8<<11));
    int found=-1;
    for(int i=0;i<dirs[5].unk4;++i) if(entries[i].unk0==scenario.battle) {found=i;break;}
    if(found<0) port_fatal("scenario: battle %d is absent on disc %d",scenario.battle,scenario.disc);
    d->unk7FC[0]=entries[found].unk4;
    d->unk804[0]=entries[found+1].unk4-entries[found].unk4;
    int size=d->unk804[0]<<11;if(size>0x2800) size=0x2800;
    if(size<=0) port_fatal("scenario: invalid encounter bundle extent");
    u8 *buffer=malloc((unsigned)size);
    if(!buffer) port_fatal("scenario: no memory for encounter metadata");
    func_80022C60(g_gameState->unk20+d->unk7FC[0],size,buffer,NULL);
    while(func_80022B18()) {}
    FileTable *table=(FileTable *)func_8001C8B0((s32)buffer,4,0x140000,0);
    if((u8 *)table<buffer || (u8 *)table+4>buffer+size ||
       (u8 *)(table->unk4+table->unk2)>buffer+size)
        port_fatal("scenario: encounter dependency table outside metadata");
    found=0;
    for(int i=0;i<table->unk2;++i) if((table->unk4[i].unk0&0xff0000)==0xc0000) {
        if(found++) port_fatal("scenario: multiple backgrounds require explicit selection");
        d->unk7FC[1]=table->unk4[i].unk4;
        d->unk804[1]=table->unk4[i].unk8;
    }
    free(buffer);
    if(!found || d->unk804[1]<=0) port_fatal("scenario: encounter background dependency missing");
}
void port_scenario_bootstrap(void) {
    if(!scenario.enabled || scenario.consumed) return;
    scenario.consumed=1;
    /* Main has initialized allocator/file tables/GPU/GameData. Use the
     * title overlay's New Game initializer without entering its loop. */
    func_80012900(0);o14_func_800A7578();
    GameData *d=g_gameState->unk1C;
    _Static_assert(sizeof(Character)==0x90,"scenario character layout");
    load_blob(scenario.characters,d->characters,sizeof d->characters);
    load_blob(scenario.event_blob,&D_8007AEF0,sizeof D_8007AEF0);
    if(scenario.story>=0) D_8007AEF0.h[0]=(s16)scenario.story;
    for(int i=0;i<0x800;++i) if(scenario.events[i]>=0) D_8007AEF0.b[i]=(u8)scenario.events[i];
    for(int id=0;id<256;++id) if(scenario.items[id]>=0) {
        ItemSlot *slot=func_80027EDC_inline(id);
        if(!slot && scenario.items[id]) {
            for(int i=0;i<256;++i) if(!d->items.slots[i].count) {slot=&d->items.slots[i];break;}
            if(!slot) port_fatal("scenario: item inventory full");
            slot->id=(u8)id;
        }
        if(slot) slot->count=(u8)scenario.items[id];
    }
    for(int i=0;i<4;++i) {
        int ch=scenario.party[i];func_80033AF4(i,ch);if(ch<0) continue;
        Character *c=&d->characters[ch];
        for(int j=0;j<5;++j) if(scenario.equip[i][j]>=0) c->equip[j]=(u8)scenario.equip[i][j];
        c->unk34.b.unk4=func_80033280(c->unk34.b.id,(c->unkA>>4)&1,c->equip);
        if(scenario.level>0) func_80033C48(ch,scenario.level,1);
        if(scenario.hp[i]>=0) {if(scenario.hp[i]>c->maxHp) port_fatal("scenario: HP exceeds maximum");c->hp=(u16)scenario.hp[i];}
    }
    rand_init((s32)scenario.seed);g_gameState->mode=(u8)scenario.kind;
    g_gameState->unkA=1; /* ordinary battle entry from a field */
    d->fieldId=(u16)scenario.field;
    if(scenario.kind==1) o14_func_800A741C();
    else {prepare_battle_resources(d);d->unkE=(u16)scenario.battle;}
    port_log("scenario entered without title/intro: mode %d, disc %d, id %d\n",scenario.kind,scenario.disc,scenario.kind==1?scenario.field:scenario.battle);
}
