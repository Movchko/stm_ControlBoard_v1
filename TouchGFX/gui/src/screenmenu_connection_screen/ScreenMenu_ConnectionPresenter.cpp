#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionView.hpp>
#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>
#include "button.h"
#include "device_config.h"
#include "menu_ui.h"
#include "esp_manager.h"

extern PPKYCfg PPKYConfig;

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
    currentIndex = view.getSelectedIndex();
    Esp32_SetEnabled(1u);
    EspManager_RequestWifiEnable();
    refreshLine();
#endif
}

void ScreenMenu_ConnectionPresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenMenu_ConnectionPresenter::refreshLine()
{
    view.updateStatusLine(currentIndex, PPKYConfig.wifi_block != 0u);
}

void ScreenMenu_ConnectionPresenter::enterConfigScreen()
{
    MenuUi_SetConfigSession(1u);
    MenuConfig_Reset();
    Esp32_SetEnabled(1u);
    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
    app->gotoScreenMenuConfigScreenNoTransition();
}

void ScreenMenu_ConnectionPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());

    if (but == BUT_ESC) {
        Esp32_SetEnabled(0u);
        MenuUi_SetConfigSession(0u);
        app->gotoScreenMenuScreenNoTransition();
        return;
    }

    if (but == BUT_UP) {
        currentIndex = (currentIndex - 1 + 2) % 2;
        view.setSelectedIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_DOWN) {
        currentIndex = (currentIndex + 1) % 2;
        view.setSelectedIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_ENTER) {
        if (currentIndex == 0 && PPKYConfig.wifi_block != 0u) {
            refreshLine();
            return;
        }
        enterConfigScreen();
    }
}
#endif
