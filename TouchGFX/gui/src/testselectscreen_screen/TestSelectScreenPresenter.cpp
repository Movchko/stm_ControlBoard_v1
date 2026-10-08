#include <gui/testselectscreen_screen/TestSelectScreenView.hpp>
#include <gui/testselectscreen_screen/TestSelectScreenPresenter.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "menu_ui.h"
#include "panel_ui_bridge.h"
#include "rs_panel_protocol.h"
#endif

TestSelectScreenPresenter::TestSelectScreenPresenter(TestSelectScreenView& v)
    : view(v)
#ifndef SIMULATOR
    , currentIndex(0)
#endif
{
}

void TestSelectScreenPresenter::activate()
{
#ifndef SIMULATOR
    MenuUi_SetMainScreenActive(0u);
    MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_TEST_SELECT);
    currentIndex = 0;
    view.setSelectedIndex(currentIndex);
    refreshDescription();
#endif
}

void TestSelectScreenPresenter::deactivate()
{
}

#ifndef SIMULATOR
void TestSelectScreenPresenter::refreshDescription()
{
    view.updateDescription(currentIndex);
}

void TestSelectScreenPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    if (but == BUT_ESC) {
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_ROOT, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }

    if (but == BUT_UP) {
        currentIndex = (int16_t)((currentIndex - 1 + TEST_COUNT) % TEST_COUNT);
        view.setSelectedIndex(currentIndex);
        refreshDescription();
        return;
    }

    if (but == BUT_DOWN) {
        currentIndex = (int16_t)((currentIndex + 1) % TEST_COUNT);
        view.setSelectedIndex(currentIndex);
        refreshDescription();
        return;
    }

    if (but == BUT_ENTER) {
        if (currentIndex == 0) {
            /* ТЕСТ1: существующий тест индикации (wipe + звук + змейка LED). */
            view.startIndicationTest();
            return;
        }
        if (currentIndex == 1) {
            /* ТЕСТ2: ручное переключение ламп. */
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_TEST_LAMPS, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
        if (currentIndex == 2) {
            /* ТЕСТ3: проверка звуков. */
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_TEST_SOUND, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
        return;
    }
}

void TestSelectScreenPresenter::onAppTick()
{
    if (MenuUi_ConsumeIndicationTestRequest() != 0u) {
        view.startIndicationTest();
    }
}
#endif
