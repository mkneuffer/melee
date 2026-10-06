/**
 * @file
 * The Dolphin SDK functions the game calls that Aurora doesn't provide.
 * The port's build includes this in every source (cmake/Port.cmake); they're
 * implemented in port_os.c.
 */

#ifndef PORT_PORT_COMPAT_H
#define PORT_PORT_COMPAT_H

#include <dolphin/card.h>
#include <dolphin/gx.h>
#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The XFB's width, padded as the video interface needs.
u16 VIPadFrameBufferWidth(u16 width);

/// Aurora runs draw-done callbacks as soon as they're set: nothing to wait
/// for.
void GXWaitDrawDone(void);

/// Aurora samples the controllers when the game reads them.
void PADSetSamplingRate(u32 msec);

/// Range-adjusted fog: not drawn by Aurora; the table is left flat.
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, const f32 projmtx[4][4]);

/// Aurora's memory cards are files it creates formatted.
s32 CARDFormatAsync(s32 chan, CARDCallback callback);

#ifdef __cplusplus
}
#endif

#endif
