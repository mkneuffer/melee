/**
 * @file
 * The THP movie decoder, which the port doesn't have yet: the game's
 * (libs/dolphin/src/dolphin/thp) is written for the console's CPU, its cache
 * instructions and 32-bit pointers. Every frame fails to decode, which the
 * game shows as black; the movies (the attract mode's) play silent and dark.
 *
 * @todo Decode THP's frames, which are JPEG, with ImageIO.
 */

#include <dolphin/thp/thp.h>

void THPInit(void) {}

s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out)
{
    (void) data;
    (void) out;
    return 0;
}

s32 THPDec_8032FD40(THPDec_8032FD40_Data* data, u16 height)
{
    (void) data;
    (void) height;
    /* Something to allocate: the decoder's work area */
    return 32;
}

s32 THPDec_803302EC(u8** data)
{
    (void) data;
    return 0;
}

THPFileInfo* THPVideoDecode(void* header, void* status_out, THPFileInfo* work,
                            void* data, THPDec_8032FD40_Data* desc)
{
    (void) header;
    (void) status_out;
    (void) work;
    (void) data;
    (void) desc;
    return NULL;
}

void THPDec_80331340(THPFileInfo* info, void* y, void* u, void* v)
{
    (void) info;
    (void) y;
    (void) u;
    (void) v;
}

void THPDec_803313D0(THPFileInfo* info, void* y, void* u, void* v, u32 n)
{
    (void) info;
    (void) y;
    (void) u;
    (void) v;
    (void) n;
}
