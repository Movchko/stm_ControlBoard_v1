#include "panel_host_cache.h"

#include <string.h>

#include "beeper.h"
#include "menu_ui.h"

static PanelHostCache s_host;

void PanelHostCache_Init(void)
{
    memset(&s_host, 0, sizeof(s_host));
    s_host.beep = 1u;
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
    const uint8_t prev_beep = s_host.beep;

    s_host.beep = on;
    s_host.beep_block = blk;
    MenuUi_SetSoundValue(on, blk);
    if (prev_beep != on) {
        Beeper_SoundOnOff(on != 0u);
    }
}
