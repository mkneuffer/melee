/**
 * @file
 * The parts of the OS and VI libraries Aurora leaves to the game: the
 * interrupt lock and the video retrace interrupt.
 *
 * The game paces itself by the retrace: its post-retrace callback samples
 * the controllers into a queue, and the main loop (gmscene.c) waits for that
 * queue before running a frame of logic. So the retrace runs here on its own
 * thread at the console's rate, and "disabling interrupts" holds the lock it
 * runs its callbacks under, as the interrupt handler would have run them
 * between the game's instructions.
 */

#include "port_internal.h"

#include <string.h>

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#include <dolphin/os.h>
#include <dolphin/vi.h>

/// NTSC's field rate: 60000/1001 Hz.
#define RETRACE_PERIOD_NS 16683350ULL

static pthread_mutex_t intr_lock = PTHREAD_MUTEX_INITIALIZER;
/// Whether this thread holds intr_lock, i.e. has interrupts disabled.
static _Thread_local bool intr_disabled;

BOOL OSDisableInterrupts(void)
{
    bool was_enabled = !intr_disabled;
    if (was_enabled) {
        pthread_mutex_lock(&intr_lock);
        intr_disabled = true;
    }
    return was_enabled;
}

BOOL OSEnableInterrupts(void)
{
    bool was_enabled = !intr_disabled;
    if (!was_enabled) {
        intr_disabled = false;
        pthread_mutex_unlock(&intr_lock);
    }
    return was_enabled;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    return level ? OSEnableInterrupts() : OSDisableInterrupts();
}

/* --- Retrace --------------------------------------------------------------
 */

static VIRetraceCallback pre_retrace_cb;
static VIRetraceCallback post_retrace_cb;
static _Atomic u32 retrace_count;
static void* next_frame_buffer;
static void* current_frame_buffer;

static pthread_mutex_t retrace_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t retrace_cond = PTHREAD_COND_INITIALIZER;
static pthread_t retrace_thread;
static _Atomic bool retrace_running;
static _Atomic bool retrace_paused;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void sleep_until(uint64_t deadline)
{
    uint64_t now = now_ns();
    if (deadline > now) {
        uint64_t ns = deadline - now;
        struct timespec ts = {
            .tv_sec = (time_t) (ns / 1000000000ULL),
            .tv_nsec = (long) (ns % 1000000000ULL),
        };
        nanosleep(&ts, NULL);
    }
}

static void retrace(void)
{
    u32 count;

    /* As the interrupt: nothing the game does with interrupts disabled sees
     * the callbacks run halfway */
    pthread_mutex_lock(&intr_lock);
    intr_disabled = true;

    count = atomic_fetch_add(&retrace_count, 1) + 1;
    if (pre_retrace_cb != NULL) {
        pre_retrace_cb(count);
    }
    current_frame_buffer = next_frame_buffer;
    if (post_retrace_cb != NULL) {
        post_retrace_cb(count);
    }

    intr_disabled = false;
    pthread_mutex_unlock(&intr_lock);

    pthread_mutex_lock(&retrace_mutex);
    pthread_cond_broadcast(&retrace_cond);
    pthread_mutex_unlock(&retrace_mutex);
}

static void* retrace_main(void* arg)
{
    uint64_t deadline = now_ns();
    (void) arg;

    pthread_setname_np("Retrace");
    while (atomic_load(&retrace_running)) {
        deadline += RETRACE_PERIOD_NS;
        sleep_until(deadline);
        if (atomic_load(&retrace_paused)) {
            /* Don't run the game's clock while paused, nor catch up after */
            deadline = now_ns();
            continue;
        }
        /* After a stall, drop the fields that were missed rather than
         * running them back to back */
        if (now_ns() > deadline + 4 * RETRACE_PERIOD_NS) {
            deadline = now_ns();
        }
        retrace();
    }
    return NULL;
}

void port_retrace_start(void)
{
    if (atomic_exchange(&retrace_running, true)) {
        return;
    }
    pthread_create(&retrace_thread, NULL, retrace_main, NULL);
}

void port_retrace_stop(void)
{
    if (!atomic_exchange(&retrace_running, false)) {
        return;
    }
    pthread_join(retrace_thread, NULL);
}

void port_retrace_set_paused(bool paused)
{
    atomic_store(&retrace_paused, paused);
}

/// Wait for the next retrace, or `timeout_ns`, whichever is first.
static void wait_retrace(uint64_t timeout_ns)
{
    u32 start = atomic_load(&retrace_count);
    uint64_t deadline = now_ns() + timeout_ns;
    struct timespec ts;

    /* Interrupts must be on, or the retrace could never run */
    if (intr_disabled) {
        return;
    }

    pthread_mutex_lock(&retrace_mutex);
    while (atomic_load(&retrace_count) == start && now_ns() < deadline) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 2000000;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&retrace_cond, &retrace_mutex, &ts);
    }
    pthread_mutex_unlock(&retrace_mutex);
}

void port_frame_idle(void)
{
    wait_retrace(RETRACE_PERIOD_NS * 2);
}

void VIWaitForRetrace(void)
{
    wait_retrace(RETRACE_PERIOD_NS * 4);
}

u32 VIGetRetraceCount(void)
{
    return atomic_load(&retrace_count);
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
    BOOL intr = OSDisableInterrupts();
    VIRetraceCallback old = pre_retrace_cb;
    pre_retrace_cb = cb;
    OSRestoreInterrupts(intr);
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
    BOOL intr = OSDisableInterrupts();
    VIRetraceCallback old = post_retrace_cb;
    post_retrace_cb = cb;
    OSRestoreInterrupts(intr);
    return old;
}

void VISetNextFrameBuffer(void* fb)
{
    next_frame_buffer = fb;
}

void* VIGetNextFrameBuffer(void)
{
    return next_frame_buffer;
}

void* VIGetCurrentFrameBuffer(void)
{
    return current_frame_buffer;
}

/// Progressive: every field is the "next" one.
u32 VIGetNextField(void)
{
    return 1;
}

u32 VIGetDTVStatus(void)
{
    return 0;
}

/// The host clears the flat view itself; there is no screen to blank.
void VISetBlack(BOOL black)
{
    (void) black;
}

/* --- What Aurora leaves out (port_compat.h) ------------------------------
 */

u16 VIPadFrameBufferWidth(u16 width)
{
    return (u16) ((width + 15) & ~15);
}

void GXWaitDrawDone(void) {}

void PADSetSamplingRate(u32 msec)
{
    (void) msec;
}

void GXInitFogAdjTable(GXFogAdjTable* table, u16 width,
                       const f32 projmtx[4][4])
{
    (void) width;
    (void) projmtx;
    memset(table, 0, sizeof(*table));
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    if (callback != NULL) {
        callback(chan, CARD_RESULT_READY);
    }
    return CARD_RESULT_READY;
}
