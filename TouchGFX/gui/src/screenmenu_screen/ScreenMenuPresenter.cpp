#include <gui/screenmenu_screen/ScreenMenuView.hpp>
#include <gui/screenmenu_screen/ScreenMenuPresenter.hpp>
#include "button.h"
#include "gost_mode.h"
#include "menu_ui.h"

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

    if (but == BUT_ESC) {
        return;
    }

    if (but == BUT_UP) {
        return;
    }

    if (but == BUT_DOWN) {
        return;
    }

    if (but == BUT_ENTER) {
        /* В режиме RS-контроля выбор пункта меню обрабатывает master
         * через panel_state -> UI_EVT_MENU_SELECT. */
        return;
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
