/**
 * @file
 * Spatial mode: the match drawn as a diorama on the player's table.
 *
 * The game's logic runs once a frame, as ever; what changes is how it is
 * drawn. Each frame the game draws its perspective cameras (the stage and
 * fighters) once per eye, with the eye's view and frustum in place of the
 * camera's, each into the IOSurface the host gave for that eye, and then its
 * orthographic cameras (the HUD) into the flat view. Every pass is its own
 * Aurora frame, presented to its own target.
 *
 * The eyes see the stage through a frustum whose near and far planes hug a
 * sphere around the stage's blast zones, so its distant backdrops (skies,
 * far scenery, built to be seen from the game's camera only) are clipped
 * instead of hanging over the player's room.
 */

#include "port_internal.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <melee/gr/stage.h>
#include <sysdolphin/baselib/cobj.h>

#include <CoreFoundation/CoreFoundation.h>

/// The nearest the near plane comes to the eye, in meters.
#define PORT_MIN_NEAR 0.05f

/* Implemented by the port's Aurora patch (port/aurora) */
extern void aurora_set_present_target(void* iosurface, uint32_t width,
                                      uint32_t height);
extern void aurora_clear_efb(float r, float g, float b, float a);
extern void aurora_wait_idle(void);

extern void port_frame_cycle(void);
extern void port_cobj_restore(void);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rendered_cond = PTHREAD_COND_INITIALIZER;

static _Atomic bool enabled;
static PortMat4 world_from_game = {
    { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 },
};
static PortSpatialFrame pending;
static bool has_pending;
static uint64_t rendered_id;

/// The frame being drawn, taken from `pending` when its passes begin.
static PortSpatialFrame frame;
static PortPass passes[4];
static int npasses, pass_index = -1;

static _Atomic uint32_t match_camera_frame;
static _Atomic uint32_t frame_counter;

/* --- Host side ------------------------------------------------------------
 */

void port_spatial_set_stage(const PortMat4* m)
{
    pthread_mutex_lock(&lock);
    world_from_game = *m;
    pthread_mutex_unlock(&lock);
}

void port_spatial_set_enabled(bool on)
{
    atomic_store(&enabled, on);
}

bool port_spatial_enabled(void)
{
    return atomic_load(&enabled);
}

static void release_surfaces(PortSpatialFrame* f)
{
    for (uint32_t i = 0; i < 2; i++) {
        if (f->surfaces[i] != NULL) {
            CFRelease((CFTypeRef) f->surfaces[i]);
            f->surfaces[i] = NULL;
        }
    }
}

void port_spatial_submit(const PortSpatialFrame* f)
{
    pthread_mutex_lock(&lock);
    if (has_pending) {
        release_surfaces(&pending);
    }
    pending = *f;
    for (uint32_t i = 0; i < 2; i++) {
        if (pending.surfaces[i] != NULL) {
            CFRetain((CFTypeRef) pending.surfaces[i]);
        }
    }
    has_pending = true;
    pthread_mutex_unlock(&lock);
}

uint64_t port_spatial_wait_rendered(uint64_t after, uint64_t timeout_ns)
{
    struct timespec ts;
    uint64_t id;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t) (timeout_ns / 1000000000ULL);
    ts.tv_nsec += (long) (timeout_ns % 1000000000ULL);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&lock);
    while (rendered_id <= after) {
        if (pthread_cond_timedwait(&rendered_cond, &lock, &ts) != 0) {
            break;
        }
    }
    id = rendered_id > after ? rendered_id : after;
    pthread_mutex_unlock(&lock);
    return id;
}

bool port_in_match(void)
{
    uint32_t seen = atomic_load(&match_camera_frame);
    return seen != 0 && atomic_load(&frame_counter) - seen < 10;
}

void port_note_match_camera(void)
{
    atomic_store(&match_camera_frame, atomic_load(&frame_counter));
}

bool port_stage_bounds(float center[3], float* radius)
{
    float l, r, t, b, hw, hh;

    if (!port_in_match()) {
        return false;
    }
    l = Stage_GetBlastZoneLeftOffset();
    r = Stage_GetBlastZoneRightOffset();
    t = Stage_GetBlastZoneTopOffset();
    b = Stage_GetBlastZoneBottomOffset();
    if (!(r > l) || !(t > b)) {
        return false;
    }
    hw = (r - l) / 2;
    hh = (t - b) / 2;
    center[0] = l + hw;
    center[1] = b + hh;
    center[2] = 0;
    *radius = sqrtf(hw * hw + hh * hh);
    return true;
}

/* --- Game side ------------------------------------------------------------
 */

static void begin_pass(PortPass pass)
{
    port_cobj_restore();
    if (pass == PORT_PASS_EYE0 || pass == PORT_PASS_EYE1) {
        int eye = pass - PORT_PASS_EYE0;
        /* The eye's frustum covers its whole image, whatever its aspect */
        AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
        aurora_set_present_target(frame.surfaces[eye], frame.width,
                                  frame.height);
        aurora_clear_efb(PORT_KEY_R / 255.0f, PORT_KEY_G / 255.0f,
                         PORT_KEY_B / 255.0f, 0.0f);
    } else {
        AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
        aurora_set_present_target(NULL, 0, 0);
    }
}

PortPass port_pass_begin(void)
{
    atomic_fetch_add(&frame_counter, 1);
    npasses = 0;

    if (atomic_load(&enabled)) {
        pthread_mutex_lock(&lock);
        if (has_pending) {
            frame = pending;
            has_pending = false;
            for (uint32_t i = 0; i < frame.eye_count && i < 2; i++) {
                passes[npasses++] = (PortPass) (PORT_PASS_EYE0 + i);
            }
        }
        pthread_mutex_unlock(&lock);
    }
    passes[npasses++] = PORT_PASS_FLAT;

    pass_index = 0;
    begin_pass(passes[0]);
    return passes[0];
}

PortPass port_pass_next(void)
{
    PortPass done = passes[pass_index];

    if (++pass_index >= npasses) {
        /* The flat pass is presented by the game's own HSD_VICopyXFBAsync */
        pass_index = -1;
        return PORT_PASS_DONE;
    }

    /* End this pass's frame, presenting it to its target */
    port_frame_cycle();

    if (done != PORT_PASS_FLAT && passes[pass_index] == PORT_PASS_FLAT) {
        /* The eyes are drawn: once the GPU has them, the host may show
         * them */
        aurora_wait_idle();
        pthread_mutex_lock(&lock);
        rendered_id = frame.id;
        release_surfaces(&frame);
        pthread_cond_broadcast(&rendered_cond);
        pthread_mutex_unlock(&lock);
    }

    begin_pass(passes[pass_index]);
    return passes[pass_index];
}

PortPass port_pass_current(void)
{
    return pass_index < 0 ? PORT_PASS_FLAT : passes[pass_index];
}

bool port_pass_draws_camera(int projection_type)
{
    bool spatial = npasses > 1;
    switch (port_pass_current()) {
    case PORT_PASS_FLAT:
        return !spatial || projection_type == PROJ_ORTHO;
    case PORT_PASS_EYE0:
    case PORT_PASS_EYE1:
        return projection_type != PROJ_ORTHO;
    default:
        return true;
    }
}

static const PortEye* current_eye(void)
{
    switch (port_pass_current()) {
    case PORT_PASS_EYE0:
        return &frame.eyes[0];
    case PORT_PASS_EYE1:
        return &frame.eyes[1];
    default:
        return NULL;
    }
}

/// c = a * b, column-major.
static void mat4_mul(float* c, const float* a, const float* b)
{
    float r[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) {
                s += a[k * 4 + row] * b[col * 4 + k];
            }
            r[col * 4 + row] = s;
        }
    }
    memcpy(c, r, sizeof(r));
}

/// The eye's view of the game's world: game units to the eye's space, in
/// meters.
static void eye_from_game(const PortEye* eye, float* out)
{
    pthread_mutex_lock(&lock);
    mat4_mul(out, eye->view.m, world_from_game.m);
    pthread_mutex_unlock(&lock);
}

bool port_pass_eye_view(Mtx view)
{
    const PortEye* eye = current_eye();
    float m[16];

    if (eye == NULL) {
        return false;
    }
    eye_from_game(eye, m);
    /* Mtx is row-major 3x4 */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            view[row][col] = m[col * 4 + row];
        }
    }
    return true;
}

bool port_pass_eye_projection(Mtx44 projection)
{
    const PortEye* eye = current_eye();
    float m[16], center[3], radius, n, f, dist, scale;

    if (eye == NULL) {
        return false;
    }

    n = PORT_MIN_NEAR;
    f = 100.0f;
    if (port_stage_bounds(center, &radius)) {
        eye_from_game(eye, m);
        /* The stage's center in the eye's space, and its radius in meters
         * (the placement scales uniformly) */
        dist = -(m[2] * center[0] + m[6] * center[1] + m[10] * center[2] +
                 m[14]);
        scale = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        radius *= scale;
        n = fmaxf(PORT_MIN_NEAR, dist - radius);
        f = fmaxf(n + 0.1f, dist + radius);
    }

    MTXFrustum(projection, eye->tan_top * n, eye->tan_bottom * n,
               eye->tan_left * n, eye->tan_right * n, n, f);
    return true;
}
