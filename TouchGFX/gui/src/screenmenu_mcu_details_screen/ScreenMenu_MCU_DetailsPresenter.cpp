#include <gui/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsView.hpp>
#include <gui/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>
#include "button.h"
#include "menu_ui.h"

ScreenMenu_MCU_DetailsPresenter::ScreenMenu_MCU_DetailsPresenter(ScreenMenu_MCU_DetailsView& v)
    : view(v)
{
}

void ScreenMenu_MCU_DetailsPresenter::activate()
{
#ifndef SIMULATOR
    view.refreshDeviceList();
    view.selectCfgSlot(MenuUi_GetMcuDetailSlot());
#endif
}

void ScreenMenu_MCU_DetailsPresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenMenu_MCU_DetailsPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());

    if (but == BUT_ESC) {
        app->gotoScreenDevicesScreenNoTransition();
        return;
    }
    if (but == BUT_UP) {
        view.prevDevice();
        MenuUi_SetMcuDetailSlot(view.getSelectedCfgSlot());
        return;
    }
    if (but == BUT_DOWN) {
        view.nextDevice();
        MenuUi_SetMcuDetailSlot(view.getSelectedCfgSlot());
        return;
    }
}
#endif
