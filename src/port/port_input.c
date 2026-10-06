/**
 * @file
 * The host's controllers, as GameCube controllers: the host maps its own
 * (PS VR2 Sense controllers, gamepads) and sets each port's state, which
 * Aurora's PADRead() returns as a virtual controller.
 */

#include "port_internal.h"

#include <dolphin/os.h>
#include <dolphin/pad.h>

void port_pad_set(uint32_t port, const PortPad* pad)
{
    PADStatus status = { 0 };
    BOOL intr;

    if (port >= PAD_CHANMAX) {
        return;
    }

    /* The retrace samples the controllers with interrupts off: don't let it
     * see half of a state */
    intr = OSDisableInterrupts();
    if (!pad->connected) {
        PADClearVirtualStatus(port);
    } else {
        status.button = pad->buttons;
        status.stickX = pad->stick_x;
        status.stickY = pad->stick_y;
        status.substickX = pad->cstick_x;
        status.substickY = pad->cstick_y;
        status.triggerLeft = pad->trigger_l;
        status.triggerRight = pad->trigger_r;
        status.analogA = (pad->buttons & PAD_BUTTON_A) ? 0xFF : 0;
        status.analogB = (pad->buttons & PAD_BUTTON_B) ? 0xFF : 0;
        PADSetVirtualStatus(port, &status);
    }
    OSRestoreInterrupts(intr);
}
