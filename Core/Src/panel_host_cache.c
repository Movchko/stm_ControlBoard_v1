#include "panel_host_cache.h"

#include <string.h>

#include "beeper.h"
#include "menu_ui.h"

static PanelHostCache s_host;

void PanelHostCache_Init(void)
{
    memset(&s_host, 0, sizeof(s_host));
    s_host.beep = 1u;
    MenuUi_SetSoundValue(1u, 0u);
}

PanelHostCache *PanelHostCache_Get(void)
{
    return &s_host;
}

const PanelHostCache *PanelHostCache_GetConst(void)
{
    return &s_host;
}

void PanelHostCache_ApplySoundState(uint8_t sound_on, uint8_t blocked)
{
    const uint8_t on = (sound_on != 0u) ? 1u : 0u;
    const uint8_t blk = (blocked != 0u) ? 1u : 0u;

    s_host.beep_block = blk;

    if (on != 0u) {
        Beeper_AckGostResumeFromHost();
        Beeper_SoundOnOff(true);
        s_host.beep = 1u;
        MenuUi_SetSoundValue(1u, blk);
        return;
    }

    /* SOUND_ON=0: не глушить сразу после GOST-unmute по неисправности —
     * FAULT в Stream идёт до SYS в Body, маска на ППКУ может отстать на кадр. */
    if (Beeper_IsGostResumeHold() != 0u) {
        s_host.beep = 1u;
        MenuUi_SetSoundValue(1u, blk);
        return;
    }

    Beeper_SoundOnOff(false);
    s_host.beep = 0u;
    MenuUi_SetSoundValue(0u, blk);
}
