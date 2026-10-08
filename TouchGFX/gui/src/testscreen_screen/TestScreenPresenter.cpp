#include <gui/testscreen_screen/TestScreenView.hpp>
#include <gui/testscreen_screen/TestScreenPresenter.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "led.h"
#include "menu_ui.h"
#include "panel_ui_bridge.h"
#include "rs_panel_protocol.h"
#endif

TestScreenPresenter::TestScreenPresenter(TestScreenView& v)
    : view(v)
#ifndef SIMULATOR
    , currentIndex(0)
#endif
{
}

void TestScreenPresenter::activate()
{
#ifndef SIMULATOR
    MenuUi_SetMainScreenActive(0u);
    MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_TEST_LAMPS);
    Led_EnterTestMode();
    currentIndex = 0;
    view.setSelectedIndex(currentIndex);
    refreshStatus();
#endif
}

void TestScreenPresenter::deactivate()
{
#ifndef SIMULATOR
    Led_ExitTestMode();
#endif
}

#ifndef SIMULATOR
void TestScreenPresenter::refreshStatus()
{
    uint8_t on = (Led_TestGet((uint8_t)currentIndex) != 0u) ? 1u : 0u;
    view.updateStatusLine(currentIndex, on);
}

void TestScreenPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    if (but == BUT_ESC) {
        /* Восстановление индикации — в deactivate → Led_ExitTestMode. */
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_TEST_SELECT, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }

    if (but == BUT_UP) {
        currentIndex = (int16_t)((currentIndex - 1 + TestScreenView::LAMP_COUNT) %
                                 TestScreenView::LAMP_COUNT);
        view.setSelectedIndex(currentIndex);
        refreshStatus();
        return;
    }

    if (but == BUT_DOWN) {
        currentIndex = (int16_t)((currentIndex + 1) % TestScreenView::LAMP_COUNT);
        view.setSelectedIndex(currentIndex);
        refreshStatus();
        return;
    }

    if (but == BUT_ENTER) {
        uint8_t cur = Led_TestGet((uint8_t)currentIndex);
        uint8_t next = (cur != 0u) ? 0u : 1u;
        Led_TestSet((uint8_t)currentIndex, next);
        refreshStatus();
        return;
    }
}
#endif
