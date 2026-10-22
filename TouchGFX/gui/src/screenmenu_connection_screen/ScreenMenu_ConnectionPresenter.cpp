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
    uint8_t id = MenuUi_GetConnectionSelected();
    if (MenuUi_ConnIdIsVisible(id) == 0u) {
        id = MENU_CONN_ID_RS485;
        MenuUi_SetConnectionSelected(id);
    }
    view.applyVisibleItemCount(MenuUi_ConnVisibleCount());
    currentIndex = (int16_t)MenuUi_ConnIdToVisible(id);
    view.setSelectedIndex(currentIndex);
    refreshLine();
#endif
}

void ScreenMenu_ConnectionPresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenMenu_ConnectionPresenter::refreshLine()
{
    const uint8_t id = MenuUi_ConnVisibleToId((uint8_t)currentIndex);
    view.updateStatusLine((int16_t)id, MenuUi_IsWifiBlocked() != 0u,
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

    const uint8_t n = MenuUi_ConnVisibleCount();
    if (but == BUT_UP) {
        currentIndex = (int16_t)((currentIndex - 1 + (int16_t)n) % (int16_t)n);
        MenuUi_SetConnectionSelected(MenuUi_ConnVisibleToId((uint8_t)currentIndex));
        view.setSelectedIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_DOWN) {
        currentIndex = (int16_t)((currentIndex + 1) % (int16_t)n);
        MenuUi_SetConnectionSelected(MenuUi_ConnVisibleToId((uint8_t)currentIndex));
        view.setSelectedIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_ENTER) {
        /* Надпись из EspManager_IsUserWifiOn / rs485_on.
         * v3: toggle+EVT в DrainPanelState (CONFIRM) — здесь только refresh.
         * legacy: кэш обновляем локально (хост шлёт UI_DATA). */
        if (RsPanelV3Slave_IsV3Active() == 0u) {
            const uint8_t id = MenuUi_ConnVisibleToId((uint8_t)currentIndex);
            if (id == MENU_CONN_ID_WIFI) {
                if (MenuUi_IsWifiBlocked() != 0u) {
                    refreshLine();
                    return;
                }
                uint8_t on = (EspManager_IsUserWifiOn() != 0u) ? 0u : 1u;
                PanelConnectionCache_SetRemoteStatus(on, PanelHostCache_GetConst()->rs485_on);
            } else if (id == MENU_CONN_ID_RS485) {
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
    const uint8_t n = MenuUi_ConnVisibleCount();
    view.applyVisibleItemCount(n);

    uint8_t id = MenuUi_GetConnectionSelected();
    if (MenuUi_ConnIdIsVisible(id) == 0u) {
        id = MENU_CONN_ID_RS485;
        MenuUi_SetConnectionSelected(id);
    }
    const int16_t desired = (int16_t)MenuUi_ConnIdToVisible(id);
    if (desired != currentIndex) {
        currentIndex = desired;
        view.setSelectedIndex(currentIndex);
    }
    refreshLine();
#endif
}
#endif
