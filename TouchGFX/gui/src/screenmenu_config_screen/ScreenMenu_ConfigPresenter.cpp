#include <gui/screenmenu_config_screen/ScreenMenu_ConfigView.hpp>
#include <gui/screenmenu_config_screen/ScreenMenu_ConfigPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>
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
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());

    if (but == BUT_ESC) {
        Esp32_SetEnabled(0u);
        MenuUi_SetConfigSession(0u);
        app->gotoScreenMenuConnectionScreenNoTransition();
    }
}
#endif
