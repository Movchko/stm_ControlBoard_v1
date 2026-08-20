#include <gui/screenblockzone_screen/ScreenBlockZoneView.hpp>
#include <gui/screenblockzone_screen/ScreenBlockZonePresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "device_config.h"
#include "config_ign_block_sync.h"

extern PPKYCfg PPKYConfig;
extern void SaveConfig(void);
#endif

ScreenBlockZonePresenter::ScreenBlockZonePresenter(ScreenBlockZoneView& v)
    : view(v)
{
}

void ScreenBlockZonePresenter::activate()
{
#ifndef SIMULATOR
    view.refreshZoneUi();
#endif
}

void ScreenBlockZonePresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenBlockZonePresenter::handleButton(uint8_t but, uint8_t state)
{
    (void)but;
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }
    /* В режиме RS-контроля навигацию и смену режима зон ведёт master
     * через panel_state -> UI events. */
}

void ScreenBlockZonePresenter::onAppTick()
{
    view.refreshZoneUi();
}
#endif
