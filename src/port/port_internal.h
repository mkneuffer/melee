/**
 * @file
 * What the game's own code calls on the native port (`TARGET_PC` only):
 * the frame loop, spatial render passes and archive loading.
 */

#ifndef PORT_PORT_INTERNAL_H
#define PORT_PORT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dolphin/mtx.h>

#include <sysdolphin/baselib/forward.h>

#include "port.h"
#include "port_compat.h"

/* --- Frames ---------------------------------------------------------------
 */

/// The game finished drawing a frame (HSD_VICopyXFBAsync): present it.
void port_frame_present(void);

/// The game is waiting for the next retrace's input; yield until it comes.
void port_frame_idle(void);

/* --- Spatial passes -------------------------------------------------------
 *
 * In spatial mode the game draws each frame in several passes: once per
 * eye with only its perspective cameras, then once flat with only its
 * orthographic ones. gmscene.c's loop:
 *
 *     for (pass = port_pass_begin(); pass != PORT_PASS_DONE;
 *          pass = port_pass_next())
 *     {
 *         HSD_StartRender(...); HSD_GObj_80390FC0(); ...
 *     }
 */

typedef enum PortPass {
    PORT_PASS_DONE = -1,
    PORT_PASS_FLAT = 0,
    PORT_PASS_EYE0 = 1,
    PORT_PASS_EYE1 = 2,
} PortPass;

/// Not the game's: the color an eye pass clears to, which the host keys
/// out to show the room. Near black, so edges blended into it read as the
/// outlines they would against the game's own (black) background.
#define PORT_KEY_R 1
#define PORT_KEY_G 0
#define PORT_KEY_B 1

PortPass port_pass_begin(void);
PortPass port_pass_next(void);
PortPass port_pass_current(void);

/// The gameplay camera drew: the game is in a match.
void port_note_match_camera(void);

/// Whether a camera of this projection draws in the current pass.
bool port_pass_draws_camera(int projection_type);

/// In an eye pass, the eye's view (game world to eye, in game units) and
/// projection, replacing the camera's. Return false otherwise.
bool port_pass_eye_view(Mtx view);
bool port_pass_eye_projection(Mtx44 projection);

/* --- Archives -------------------------------------------------------------
 */

/// The name of the file the next HSD_ArchiveParse() reads, for its roots.
void port_dat_set_next_file(const char* name);

/// HSD_Archive on the port: the data stays in its original byte order and
/// is converted into native objects as the game asks for them.
int32_t port_dat_parse(HSD_Archive* archive, uint8_t* src, size_t size);
void* port_dat_public(HSD_Archive* archive, const char* name,
                      const char* type_name);
char* port_dat_extern(HSD_Archive* archive, int index);
void port_dat_locate_extern(HSD_Archive* archive, const char* name,
                            void* addr);
/// How many bytes of the archive's raw data follow `ptr`, for arrays whose
/// length the data doesn't say (GXSetArray); 0 if `ptr` isn't in one.
uint32_t port_dat_raw_extent(const void* ptr);

#endif
