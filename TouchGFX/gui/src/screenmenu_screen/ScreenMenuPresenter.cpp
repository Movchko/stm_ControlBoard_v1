#include <gui/screenmenu_screen/ScreenMenuView.hpp>
#include <gui/screenmenu_screen/ScreenMenuPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>
#include "button.h"
#include "device_config.h"
#include "event_log.h"
#include "gost_mode.h"
extern PPKYCfg PPKYConfig;
extern void SaveConfig(void);

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
    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
    soundOn = (PPKYConfig.beep != 0u);
    app->getModel().setSoundOn(soundOn);
    currentIndex = view.getSelectedMenuIndex();
    if (currentIndex >= MENU_ITEMS) {
        currentIndex = 0;
        view.setMenuIndex(currentIndex);
    }
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
    view.updateParameterLine(menuActionIndex(currentIndex), PPKYConfig.fire_mode, soundOn,
			     PPKYConfig.beep_block != 0u);
}

void ScreenMenuPresenter::SetupMenuChangePos(unsigned char val) {
    view.SetupMenuChangePos(val);
}

void ScreenMenuPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());

    if (but == BUT_ESC) {
        app->gotomainscreenScreenNoTransition();
        return;
    }

    if (but == BUT_UP) {
        currentIndex = (currentIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
        view.setMenuIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_DOWN) {
        currentIndex = (currentIndex + 1) % MENU_ITEMS;
        view.setMenuIndex(currentIndex);
        refreshLine();
        return;
    }

    if (but == BUT_ENTER) {
        const int action = menuActionIndex(currentIndex);
        if (action == 0) {
            uint8_t mode = (uint8_t)((PPKYConfig.fire_mode + 1u) % 3u);
            PPKYConfig.fire_mode = mode;
            SaveConfig();
            EventLog_LogFireModeChange(mode, 0u); /* source: menu */
            refreshLine();
            return;
        }
        if (action == 1) {
            if (PPKYConfig.beep_block != 0u) {
                refreshLine();
                return;
            }
            soundOn = !soundOn;
            PPKYConfig.beep = soundOn ? 1u : 0u;
            SaveConfig();
            EventLog_LogSoundToggle(soundOn ? 1u : 0u, 0u); /* source: menu */
            app->getModel().setSoundOn(soundOn);
            app->getModel().notifySoundToggled(soundOn);
            refreshLine();
            return;
        }
        if (action == 2) {
            app->gotoScreenMenuConnectionScreenNoTransition();
            return;
        }
        if (action == 3) {
            app->gotoScreenMenuJurnalScreenNoTransition();
            return;
        }
        if (action == 4) {
            app->gotoScreenDevicesScreenNoTransition();
            return;
        }
        if (action == 5) {
            app->gotoScreenBlockZoneScreenNoTransition();
            return;
        }
        if (action == 6) {
            /* Экранный wipe по тикам; звук + змейка — после него в View. */
            view.startIndicationTest();
            return;
        }
    }
}
#endif
