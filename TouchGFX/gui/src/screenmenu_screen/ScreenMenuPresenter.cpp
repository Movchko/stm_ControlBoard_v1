#include <gui/screenmenu_screen/ScreenMenuView.hpp>
#include <gui/screenmenu_screen/ScreenMenuPresenter.hpp>
#include "button.h"
#include "gost_mode.h"
#include "menu_ui.h"
#include "rs_panel_protocol.h"
#include "panel_ui_bridge.h"
#include "rs_panel_v3_slave.h"
#include "panel_host_cache.h"
#include "event_log.h"

ScreenMenuPresenter::ScreenMenuPresenter(ScreenMenuView& v)
    : view(v)
{
#ifndef SIMULATOR
    soundOn = true;
    currentIndex = 0;
#endif
}

void ScreenMenuPresenter::activate()
{
#ifndef SIMULATOR
    MenuUi_SetMainScreenActive(0u);
    MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_ROOT);
    soundOn = (MenuUi_GetSoundValue() != 0u);
    currentIndex = (int16_t)MenuUi_GetMenuSelected();
    if (currentIndex >= MENU_ITEMS) {
        currentIndex = 0;
    }
    view.setMenuIndex(currentIndex);
    refreshLine();
#endif
}

void ScreenMenuPresenter::deactivate()
{
#ifndef SIMULATOR
    /* session не сбрасываем: переход в подменю уже выставил новый screen_id;
     * сброс на MAIN делает PanelUiBridge / mainscreenPresenter::activate. */
#endif
}

#ifndef SIMULATOR
int ScreenMenuPresenter::menuActionIndex(int16_t logical_index)
{
#if GOST_MODE
	/* Логические 0..5 → действия 1..6 (пропуск глобального «РЕЖИМ»). */
	return (int)logical_index + 1;
#else
	return (int)logical_index;
#endif
}

void ScreenMenuPresenter::refreshLine()
{
    view.updateParameterLine(menuActionIndex(currentIndex),
                             MenuUi_GetFireModeValue(),
                             soundOn,
                             MenuUi_IsSoundBlocked() != 0u);
}

void ScreenMenuPresenter::SetupMenuChangePos(unsigned char val) {
    view.SetupMenuChangePos(val);
}

void ScreenMenuPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    /* Локальная навигация UI. События на ППКУ (при v3) — через panel_state/Drain. */
    if (but == BUT_ESC) {
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MAIN, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }

    if (but == BUT_UP) {
        currentIndex = (int16_t)((currentIndex - 1 + MENU_ITEMS) % MENU_ITEMS);
        MenuUi_SetMenuSelected((uint16_t)currentIndex);
        view.setMenuIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_DOWN) {
        currentIndex = (int16_t)((currentIndex + 1) % MENU_ITEMS);
        MenuUi_SetMenuSelected((uint16_t)currentIndex);
        view.setMenuIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_ENTER) {
        const int action = menuActionIndex(currentIndex);
        if (action == 0) {
            uint8_t mode = (uint8_t)((MenuUi_GetFireModeValue() + 1u) % 3u);
            MenuUi_SetFireModeValue(mode);
            PanelHostCache_Get()->fire_mode = mode;
            EventLog_LogFireModeChange(mode, 0u);
            refreshLine();
            return;
        }
        if (action == 1) {
            if (MenuUi_IsSoundBlocked() != 0u) {
                refreshLine();
                return;
            }
            soundOn = !soundOn;
            PanelHostCache_Get()->beep = soundOn ? 1u : 0u;
            MenuUi_SetSoundValue(soundOn ? 1u : 0u, MenuUi_IsSoundBlocked());
            EventLog_LogSoundToggle(soundOn ? 1u : 0u, 0u);
            /* Оптимистично локально; ППКУ подтвердит SYS flags / при DENIED откатит. */
            if (RsPanelV3Slave_IsV3Active() != 0u) {
                (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_SOUND_SET, 0u,
                                               soundOn ? 1u : 0u, 0u, 0u);
            }
            if (model) {
                model->setSoundOn(soundOn);
                model->notifySoundToggled(soundOn);
            }
            if (soundOn) {
                RsPanelV3Slave_OnSoundEnabled();
            }
            refreshLine();
            return;
        }
        if (action == 2) {
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_CONNECTION, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
        if (action == 3) {
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_JOURNAL, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
        if (action == 4) {
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_DEVICES, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
        if (action == 5) {
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_BLOCK_ZONE, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
        if (action == 6) {
            PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_TEST_SELECT, RS_PANEL_UI_ACTION_REPLACE);
            return;
        }
    }
}

void ScreenMenuPresenter::onAppTick()
{
#ifndef SIMULATOR
    if (MenuUi_ConsumeIndicationTestRequest() != 0u) {
        view.startIndicationTest();
    }

    soundOn = (MenuUi_GetSoundValue() != 0u);

    const uint16_t sel = MenuUi_GetMenuSelected();
    int16_t desired = (int16_t)sel;

    if (desired < 0) {
        desired = 0;
    }
    if (desired >= MENU_ITEMS) {
        desired = (int16_t)(MENU_ITEMS - 1u);
    }

    if (desired != currentIndex) {
        currentIndex = desired;
        view.setMenuIndex(currentIndex);
    }
    refreshLine();
#endif
}

void ScreenMenuPresenter::onSoundOnChanged(bool soundOnIn)
{
#ifndef SIMULATOR
    (void)soundOnIn;
    soundOn = (MenuUi_GetSoundValue() != 0u);
    refreshLine();
#endif
}
#endif
