#include <gui/screenblockzone_screen/ScreenBlockZoneView.hpp>
#include <gui/screenblockzone_screen/ScreenBlockZonePresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "fire.h"
#include "panel_ui_bridge.h"
#include "rs_panel_protocol.h"
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

    if (but == BUT_ESC) {
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_ROOT, RS_PANEL_UI_ACTION_REPLACE);
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
        /* Цикл mode + ZONE_BLOCK_SET + SaveConfig — в DrainPanelState (CONFIRM). */
        if (Fire_IsActive() == 0u) {
            view.refreshZoneUi();
        }
    }
}

void ScreenBlockZonePresenter::onAppTick()
{
    view.refreshZoneUi();
}
#endif
