/* Host mock of the libnds calls used by render_gx.c; no rasterisation. */
#ifndef TEST_NDS_H
#define TEST_NDS_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
typedef int16_t t16;
typedef int16_t v16;
typedef enum {GL_RGB16, GL_RGB256, GL_RGBA} GL_TEXTURE_TYPE_ENUM;
#define TEXGEN_OFF 0
#define GL_TEXTURE_WRAP_S 1
#define GL_TEXTURE_WRAP_T 2
#define GL_TEXTURE_COLOR0_TRANSPARENT 4
#define POLY_ALPHA(x) (x)
#define POLY_ID(x) ((x) << 8)
#define POLY_CULL_NONE 0
#define GL_QUADS 0
#define GL_TRIANGLES 1
#define GL_TRANS_MANUALSORT 0
#define MODE_0_3D 0
#define VRAM_A_TEXTURE 0
#define VRAM_B_TEXTURE 0
#define VRAM_C_TEXTURE 0
#define VRAM_D_TEXTURE 0
#define VRAM_E_TEX_PALETTE 0
#define VRAM_F_TEX_PALETTE_SLOT4 0
#define VRAM_G_TEX_PALETTE_SLOT5 0
#define GL_TEXTURE_2D 1
#define GL_BLEND 2
#define GL_MAX_DEPTH 32767
#define GL_PROJECTION 0
#define GL_MODELVIEW 1
#define glPolyFmt(value) ((void)(value))
#define glTexCoord2t16(...) ((void)0)
void glVertex3v16(v16 x, v16 y, v16 z);
#define glColor3b(...) ((void)0)
#define glBegin(...) ((void)0)
#define glEnd(...) ((void)0)
#define glClearColor(...) ((void)0)
#define videoSetMode(...) ((void)0)
#define glInit(...) ((void)0)
#define vramSetBankA(...) ((void)0)
#define vramSetBankB(...) ((void)0)
#define vramSetBankC(...) ((void)0)
#define vramSetBankD(...) ((void)0)
#define vramSetBankE(...) ((void)0)
#define vramSetBankF(...) ((void)0)
#define vramSetBankG(...) ((void)0)
#define glEnable(...) ((void)0)
#define glClearPolyID(...) ((void)0)
#define glClearDepth(...) ((void)0)
#define glViewport(...) ((void)0)
#define glMatrixMode(...) ((void)0)
#define glLoadIdentity(...) ((void)0)
#define glOrthof32(...) ((void)0)
int glGenTextures(int n, int *names);
void glBindTexture(int target, int name);
void glDeleteTextures(int n, const int *names);
int glTexImage2D(int target, int level, GL_TEXTURE_TYPE_ENUM type, int w, int h, int border, int params, const void *data);
int glColorTableEXT(int target, int unused, size_t count, int a, int b, const void *palette);
int glAssignColorTable(int target, int name);
void glFlush(int mode);
#endif
