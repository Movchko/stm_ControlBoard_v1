#include <gui/testscreen_screen/TestScreenView.hpp>
#include <gui/testscreen_screen/TestScreenPresenter.hpp>

#ifndef SIMULATOR
#include "beeper.h"
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
    , soundMode(false)
    , lastPlaying(0u)
#endif
{
}

void TestScreenPresenter::activate()
{
#ifndef SIMULATOR
    MenuUi_SetMainScreenActive(0u);
    soundMode = (MenuUi_GetMenuSessionScreen() == RS_PANEL_SCREEN_MENU_TEST_SOUND);
    if (soundMode) {
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_TEST_SOUND);
        Beeper_EnterTestMode();
    } else {
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_TEST_LAMPS);
        Led_EnterTestMode();
    }
    currentIndex = 0;
    lastPlaying = 0u;
    view.setSoundMode(soundMode);
    view.setSelectedIndex(currentIndex);
    refreshStatus();
#endif
}

void TestScreenPresenter::deactivate()
{
#ifndef SIMULATOR
    if (soundMode) {
        Beeper_ExitTestMode();
    } else {
        Led_ExitTestMode();
    }
#endif
}

#ifndef SIMULATOR
int16_t TestScreenPresenter::itemCount() const
{
    return soundMode ? TestScreenView::SOUND_COUNT : TestScreenView::LAMP_COUNT;
}

void TestScreenPresenter::refreshStatus()
{
    if (soundMode) {
        uint8_t playing = Beeper_IsTestPlaying();
        view.updateStatusLine(currentIndex, playing);
        lastPlaying = playing;
        return;
    }
    uint8_t on = (Led_TestGet((uint8_t)currentIndex) != 0u) ? 1u : 0u;
    view.updateStatusLine(currentIndex, on);
}

void TestScreenPresenter::onAppTick()
{
    if (!soundMode) {
        return;
    }
    uint8_t playing = Beeper_IsTestPlaying();
    if (playing != lastPlaying) {
        lastPlaying = playing;
        view.updateStatusLine(currentIndex, playing);
    }
}

void TestScreenPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    const int16_t count = itemCount();

    if (but == BUT_ESC) {
        /* Restore — в deactivate (Led/Beeper ExitTestMode). */
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_TEST_SELECT, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }

    if (but == BUT_UP) {
        if (soundMode) {
            Beeper_TestStop();
        }
        currentIndex = (int16_t)((currentIndex - 1 + count) % count);
        view.setSelectedIndex(currentIndex);
        refreshStatus();
        return;
    }

    if (but == BUT_DOWN) {
        if (soundMode) {
            Beeper_TestStop();
        }
        currentIndex = (int16_t)((currentIndex + 1) % count);
        view.setSelectedIndex(currentIndex);
        refreshStatus();
        return;
    }

    if (but == BUT_ENTER) {
        if (soundMode) {
            Beeper_TestPlay((uint8_t)currentIndex);
            refreshStatus();
            return;
        }
        uint8_t cur = Led_TestGet((uint8_t)currentIndex);
        uint8_t next = (cur != 0u) ? 0u : 1u;
        Led_TestSet((uint8_t)currentIndex, next);
        refreshStatus();
        return;
    }
}
#endif
