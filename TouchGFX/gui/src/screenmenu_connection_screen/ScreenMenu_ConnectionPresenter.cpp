#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionView.hpp>
#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionPresenter.hpp>
#include "button.h"
#include "menu_ui.h"

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
    view.updateStatusLine(currentIndex, MenuUi_IsWifiBlocked() != 0u);
}

void ScreenMenu_ConnectionPresenter::handleButton(uint8_t but, uint8_t state)
{
    (void)but;
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }
    /* В режиме RS-контроля connection menu ведёт master через panel_state -> UI events. */
}

void ScreenMenu_ConnectionPresenter::onAppTick()
{
    currentIndex = (int16_t)MenuUi_GetConnectionSelected();
    view.setSelectedIndex(currentIndex);
    refreshLine();
}
#endif
