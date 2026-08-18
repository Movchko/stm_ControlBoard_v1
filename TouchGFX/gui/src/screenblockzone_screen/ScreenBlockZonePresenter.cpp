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
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    FrontendApplication *app = static_cast<FrontendApplication *>(touchgfx::Application::getInstance());

    if (but == BUT_ESC) {
        app->gotoScreenMenuScreenNoTransition();
        return;
    }
    if (but == BUT_UP) {
        view.prevActiveZone();
        return;
    }
    if (but == BUT_DOWN) {
        view.nextActiveZone();
        return;
    }
    if (but == BUT_ENTER) {
        if (view.hasActiveZones() == 0u) {
            return;
        }
        view.cycleSelectedZoneMode();
        SaveConfig();
        ConfigIgnBlockSync_Request();
    }
}
#endif
