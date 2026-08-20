#include <gui/screenmenu_config_screen/ScreenMenu_ConfigView.hpp>
#include <gui/screenmenu_config_screen/ScreenMenu_ConfigPresenter.hpp>
#include "button.h"
#include "menu_ui.h"
#include <cstdio>

ScreenMenu_ConfigPresenter::ScreenMenu_ConfigPresenter(ScreenMenu_ConfigView& v)
    : view(v)
{
#ifndef SIMULATOR
    lastState = 0xFFu;
    lastPercent = 0xFFu;
#endif
}

void ScreenMenu_ConfigPresenter::activate()
{
#ifndef SIMULATOR
    lastState = 0xFFu;
    lastPercent = 0xFFu;
    updateStatusFromApp();
#endif
}

void ScreenMenu_ConfigPresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenMenu_ConfigPresenter::updateStatusFromApp()
{
    MenuCfgState st = MenuConfig_GetState();
    uint8_t pct = MenuConfig_GetPercent();
    if ((uint8_t)st == lastState && pct == lastPercent) {
        return;
    }
    lastState = (uint8_t)st;
    lastPercent = pct;

    char line[48] = {0};
    switch (st) {
    case MENU_CFG_STATE_RECEIVING:
        (void)std::snprintf(line, sizeof(line), "Загрузка %u%%", (unsigned)pct);
        break;
    case MENU_CFG_STATE_APPLYING:
        (void)std::snprintf(line, sizeof(line), "ПРИМЕНЕНИЕ...");
        break;
    case MENU_CFG_STATE_SUCCESS:
        (void)std::snprintf(line, sizeof(line), "УСПЕШНО");
        break;
    default:
        (void)std::snprintf(line, sizeof(line), "Ожидание...");
        break;
    }
    view.setStatusText(line);
}

void ScreenMenu_ConfigPresenter::onAppTick()
{
    updateStatusFromApp();
}

void ScreenMenu_ConfigPresenter::handleButton(uint8_t but, uint8_t state)
{
    (void)but;
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }
    /* В режиме RS-контроля выход из config-экрана ведёт master
     * через panel_state -> UI_EVT_BACK. */
}
#endif
