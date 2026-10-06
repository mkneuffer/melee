/**
 * @file
 * The native port's interface to its host: the app that owns the window,
 * the immersive space and the device's tracking (`platform/visionos`).
 *
 * The game runs on its own thread, from port_run(), in its original loop.
 * The host feeds it the state of the world each frame (the stage's anchor,
 * the eyes' poses) and takes the frames it renders: the flat view, which
 * Aurora presents to the host's Metal layer, and, in spatial mode, one image
 * per eye, which the game renders into IOSurfaces the host composites.
 *
 * Every function here may be called from any thread.
 */

#ifndef PORT_PORT_H
#define PORT_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortConfig {
    /// The player's disc image (ISO, GCM, RVZ...) of GALE01 v1.02.
    const char* disc_path;
    /// Where saves (memory card images) and settings go.
    const char* user_path;
    /// Where Aurora keeps its pipeline cache.
    const char* cache_path;
    /// The CAMetalLayer the flat view presents to, owned by the host.
    void* metal_layer;
    /// The layer's size in pixels and its scale.
    uint32_t layer_width, layer_height;
    float layer_scale;
} PortConfig;

/// Run the game on the calling thread until it exits. Returns its status.
int port_run(const PortConfig* config);

/// Ask the game to exit; port_run() returns soon after.
void port_request_exit(void);

/// The host's Metal layer was resized.
void port_set_layer_size(uint32_t width, uint32_t height, float scale);

/// Pause or resume the game, as when the app is backgrounded.
void port_set_paused(bool paused);

/* --- Spatial mode ---------------------------------------------------------
 */

/// Column-major 4x4, as simd_float4x4 lays it out.
typedef struct PortMat4 {
    float m[16];
} PortMat4;

/// One eye's view of the world, as the compositor gives it.
typedef struct PortEye {
    /// The eye's view: from world space (the stage's anchor space, in
    /// meters) to the eye's space, looking down -Z.
    PortMat4 view;
    /// The tangents of the eye's half-angles: left and bottom negative.
    float tan_left, tan_right, tan_top, tan_bottom;
} PortEye;

typedef struct PortSpatialFrame {
    /// Which frame of the host's this is, echoed back with the images.
    uint64_t id;
    PortEye eyes[2];
    uint32_t eye_count;
    /// The image the game renders each eye into: an IOSurfaceRef of
    /// BGRA8, `width` by `height`. Owned by the host; the game retains
    /// them while it renders.
    void* surfaces[2];
    uint32_t width, height;
} PortSpatialFrame;

/// The stage's placement: from the game's world (its units) to the
/// anchor's space (meters), as the host computes it from where the player
/// put the table and how large they made it.
void port_spatial_set_stage(const PortMat4* world_from_game);

/// Enter or leave spatial mode. In spatial mode, each frame the game
/// renders its perspective cameras once per eye of the latest frame the
/// host submitted, and its 2D cameras (the HUD) into the flat view.
void port_spatial_set_enabled(bool enabled);
bool port_spatial_enabled(void);

/// Give the game the eyes to render its next frame with. The latest frame
/// wins; a frame the game has not started is replaced.
void port_spatial_submit(const PortSpatialFrame* frame);

/// Wait up to `timeout_ns` for the game to finish rendering a frame newer
/// than `after`, and return its id, or `after` if none finished in time.
/// When this returns a new id, that frame's surfaces hold its eyes,
/// complete on the GPU.
uint64_t port_spatial_wait_rendered(uint64_t after, uint64_t timeout_ns);

/* --- Input ----------------------------------------------------------------
 */

/// A GameCube controller's buttons, as the game reads them.
enum {
    PORT_PAD_LEFT = 0x0001,
    PORT_PAD_RIGHT = 0x0002,
    PORT_PAD_DOWN = 0x0004,
    PORT_PAD_UP = 0x0008,
    PORT_PAD_Z = 0x0010,
    PORT_PAD_R = 0x0020,
    PORT_PAD_L = 0x0040,
    PORT_PAD_A = 0x0100,
    PORT_PAD_B = 0x0200,
    PORT_PAD_X = 0x0400,
    PORT_PAD_Y = 0x0800,
    PORT_PAD_START = 0x1000,
};

/// One GameCube controller's state, as the host maps its controllers to it.
typedef struct PortPad {
    bool connected;
    uint16_t buttons;
    /// The control stick and C-stick, -100 to 100, up positive.
    int8_t stick_x, stick_y;
    int8_t cstick_x, cstick_y;
    /// How far L and R are pressed, 0 to 255; the digital L and R are
    /// buttons of their own, at the end of the triggers' travel.
    uint8_t trigger_l, trigger_r;
} PortPad;

/// Set port `port`'s (0 to 3) controller. It stays as set until set again.
void port_pad_set(uint32_t port, const PortPad* pad);

/// Whether the game is in a match, where the host opens the diorama, as
/// opposed to the menus, which stay in the flat view.
bool port_in_match(void);

/// In a match, a sphere around the stage's blast zones in the game's units:
/// what the host fits to the table when it places the stage.
bool port_stage_bounds(float center[3], float* radius);

#ifdef __cplusplus
}
#endif

#endif
