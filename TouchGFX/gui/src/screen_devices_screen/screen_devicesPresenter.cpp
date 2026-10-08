#include <gui/screen_devices_screen/screen_devicesView.hpp>
#include <gui/screen_devices_screen/screen_devicesPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "menu_ui.h"
#include "panel_ui_bridge.h"
#include "rs_panel_protocol.h"
#endif

screen_devicesPresenter::screen_devicesPresenter(screen_devicesView& v)
    : view(v)
{
}

void screen_devicesPresenter::activate()
{
#ifndef SIMULATOR
    view.refreshDeviceUi();
#endif
}

void screen_devicesPresenter::deactivate()
{
}

#ifndef SIMULATOR
void screen_devicesPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    if (but == BUT_ESC) {
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_ROOT, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }
    if (but == BUT_UP) {
        view.prevDevice();
        return;
    }
    if (but == BUT_DOWN) {
        view.nextDevice();
        return;
    }
    if (but == BUT_ENTER) {
        uint8_t slot = view.getSelectedCfgSlot();
        if (slot != 0xFFu) {
            MenuUi_SetMcuDetailSlot(slot);
            /* DEVICES_GET при v3 — через Drain (CONFIRM). */
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_DEVICE_DETAIL,
                                    RS_PANEL_UI_ACTION_REPLACE);
        }
    }
}

void screen_devicesPresenter::onAppTick()
{
    view.refreshDeviceUi();
}
#endif
