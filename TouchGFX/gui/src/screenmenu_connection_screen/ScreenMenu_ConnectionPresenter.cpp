#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionView.hpp>
#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionPresenter.hpp>
#include "button.h"
#include "menu_ui.h"
#include "esp_manager.h"
#include "panel_host_cache.h"
#include "panel_ui_bridge.h"
#include "rs_panel_protocol.h"
#include "rs_panel_v3_slave.h"


ScreenMenu_ConnectionPresenter::ScreenMenu_ConnectionPresenter(ScreenMenu_ConnectionView& v)
    : view(v)
{
#ifndef SIMULATOR
    currentIndex = 0;
#endif
}

void ScreenMenu_ConnectionPresenter::activate()
{
#ifndef SIMULATOR
    currentIndex = (int16_t)MenuUi_GetConnectionSelected();
    if (MenuUi_IsWifiBlocked() != 0u) {
        currentIndex = 1;
        view.setSelectedIndex(currentIndex);
    }
    refreshLine();
#endif
}

void ScreenMenu_ConnectionPresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenMenu_ConnectionPresenter::refreshLine()
{
    view.updateStatusLine(currentIndex, MenuUi_IsWifiBlocked() != 0u,
                          EspManager_IsUserWifiOn() != 0u,
                          PanelHostCache_GetConst()->rs485_on != 0u);
}

void ScreenMenu_ConnectionPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    if (but == BUT_ESC) {
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_ROOT, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }

    if (but == BUT_UP) {
        if (MenuUi_IsWifiBlocked() != 0u) {
            currentIndex = 1;
        } else {
            currentIndex = (int16_t)((currentIndex - 1 + 2) % 2);
        }
        MenuUi_SetConnectionSelected((uint8_t)currentIndex);
        view.setSelectedIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_DOWN) {
        if (MenuUi_IsWifiBlocked() != 0u) {
            currentIndex = 1;
        } else {
            currentIndex = (int16_t)((currentIndex + 1) % 2);
        }
        MenuUi_SetConnectionSelected((uint8_t)currentIndex);
        view.setSelectedIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_ENTER) {
        /* При v3 WIFI/EXT_CAN шлёт Drain по CONFIRM; локально только UI-кэш без хоста. */
        if (RsPanelV3Slave_IsV3Active() == 0u) {
            if (currentIndex == 0) {
                if (MenuUi_IsWifiBlocked() != 0u) {
                    refreshLine();
                    return;
                }
                uint8_t on = (EspManager_IsUserWifiOn() != 0u) ? 0u : 1u;
                PanelConnectionCache_SetRemoteStatus(on, PanelHostCache_GetConst()->rs485_on);
            } else {
                uint8_t on = (PanelHostCache_GetConst()->rs485_on != 0u) ? 0u : 1u;
                PanelConnectionCache_SetRemoteStatus(EspManager_IsUserWifiOn(), on);
            }
        }
        refreshLine();
    }
}

void ScreenMenu_ConnectionPresenter::onAppTick()
{
#ifndef SIMULATOR
    int16_t desired = (int16_t)MenuUi_GetConnectionSelected();
    if (MenuUi_IsWifiBlocked() != 0u) {
        desired = 1;
    }
    if (desired != currentIndex) {
        currentIndex = desired;
        view.setSelectedIndex(currentIndex);
    }
    refreshLine();
#endif
}
#endif
