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
    (void)but;
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }
    /* В режиме RS-контроля навигацию по деталям МКУ ведёт master
     * через panel_state -> UI events. */
}

void ScreenMenu_MCU_DetailsPresenter::onAppTick()
{
    view.refreshDeviceList();
    view.selectCfgSlot(MenuUi_GetMcuDetailSlot());
}
#endif
