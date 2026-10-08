#include "panel_host_cache.h"

#include <string.h>

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
