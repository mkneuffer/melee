/**
 * @file
 * Boots the game on the port: Aurora, the player's disc, the retrace, and
 * the game's own main(), which never returns while the game runs.
 */

#include "port_internal.h"

#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/event.h>

extern int melee_main(void);
extern void port_retrace_start(void);
extern void port_retrace_stop(void);
extern void port_retrace_set_paused(bool paused);

/* Implemented by the port's Aurora patch (port/aurora) */
extern void aurora_set_external_layer_size(uint32_t width, uint32_t height,
                                           float scale);

static _Atomic bool exit_requested;
static _Atomic bool paused;
static jmp_buf exit_jump;
static bool running;

static void log_callback(AuroraLogLevel level, const char* module,
                         const char* message, unsigned int len)
{
    static const char* const names[] = {
        "DEBUG", "INFO", "WARNING", "ERROR", "FATAL",
    };
    fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s: %s] %.*s\n",
            names[level], module, (int) len, message);
    if (level == LOG_FATAL) {
        fflush(stderr);
        abort();
    }
}

/// Handle what Aurora's events say: the host closing, pausing.
static void pump_events(void)
{
    const AuroraEvent* event = aurora_update();
    while (event != NULL && event->type != AURORA_NONE) {
        if (event->type == AURORA_EXIT) {
            atomic_store(&exit_requested, true);
        }
        event++;
    }
}

/// End the frame Aurora has open, presenting it to its target, and begin
/// the next. The game's thread always has a frame open.
void port_frame_cycle(void)
{
    aurora_end_frame();
    pump_events();
    while (!aurora_begin_frame()) {
        /* Not presentable: backgrounded, or the layer is gone. Wait as the
         * retrace does, without running the game */
        port_frame_idle();
        pump_events();
        if (atomic_load(&exit_requested)) {
            break;
        }
    }
}

void port_frame_present(void)
{
    port_frame_cycle();

    while (atomic_load(&paused) && !atomic_load(&exit_requested)) {
        port_frame_idle();
        pump_events();
    }
    if (atomic_load(&exit_requested) && running) {
        /* The game has no way out of its loop: leave it from here, between
         * frames, where nothing is half done */
        longjmp(exit_jump, 1);
    }
}

int port_run(const PortConfig* config)
{
    AuroraConfig aurora = {
        .appName = "Melee",
        .userPath = config->user_path,
        .cachePath = config->cache_path,
        .desiredBackend = BACKEND_METAL,
        .vsync = true,
        .logCallback = log_callback,
        .logLevel = LOG_INFO,
        .mem1Size = MEM1_DEFAULT_SIZE,
        .mem2Size = ARAM_DEFAULT_SIZE,
        .externalMetalLayer = config->metal_layer,
    };
    static char* argv[] = { "melee", NULL };
    int status = 0;

    aurora_set_external_layer_size(config->layer_width, config->layer_height,
                                   config->layer_scale);
    aurora_initialize(1, argv, &aurora);

    if (!aurora_dvd_open(config->disc_path)) {
        fprintf(stderr, "port: can't open the disc at %s\n",
                config->disc_path);
        aurora_shutdown();
        return 1;
    }

    while (!aurora_begin_frame()) {
        pump_events();
    }
    port_retrace_start();

    running = true;
    if (setjmp(exit_jump) == 0) {
        status = melee_main();
    }
    running = false;

    port_retrace_stop();
    aurora_dvd_close();
    aurora_shutdown();
    return status;
}

void port_request_exit(void)
{
    atomic_store(&exit_requested, true);
}

void port_set_layer_size(uint32_t width, uint32_t height, float scale)
{
    aurora_set_external_layer_size(width, height, scale);
}

void port_set_paused(bool p)
{
    atomic_store(&paused, p);
    port_retrace_set_paused(p);
}
