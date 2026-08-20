#include <gui/mainscreen_screen/mainscreenView.hpp>
#include <gui/mainscreen_screen/mainscreenPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "fire.h"
#include "menu_ui.h"
#include "esp_manager.h"
#endif

mainscreenPresenter::mainscreenPresenter(mainscreenView& v)
    : view(v)
{

}

void mainscreenPresenter::activate()
{
#ifndef SIMULATOR
    MenuUi_SetMainScreenActive(1u);
    /* Один раз при входе на экран — без последующего опроса в tick. */
    if (model) {
        view.applyMuteIcon(model->getSoundOn());
        view.applyWifiIcon(EspManager_IsLinkActive() != 0u);
    }
#endif
}

void mainscreenPresenter::deactivate()
{
#ifndef SIMULATOR
    MenuUi_SetMainScreenActive(0u);
#endif
}

void mainscreenPresenter::setDateTime(uint8_t hour, uint8_t min, uint8_t sec, uint8_t day, uint8_t month, uint8_t year)
{
    view.setDateTime(hour, min, sec, day, month, year);
}

#ifndef SIMULATOR
void mainscreenPresenter::SetTime(uint32_t time) {
	view.SetTime(time);
}

void mainscreenPresenter::handleButton(uint8_t but, uint8_t state)
{
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    if (but == BUT_ENTER)
    {
        return;
    }

    if (but == BUT_UP || but == BUT_DOWN || but == BUT_ESC) {
        view.handleMainNavButton(but);
    }
}

void mainscreenPresenter::onFireStatusChanged(bool active, uint8_t mode, uint8_t zone, uint8_t remaining_s, uint8_t nZoneNames,
					      char (*zoneNames)[ZONE_NAME_SIZE + 1])
{
	view.updateFireStatus(active, mode, zone, remaining_s, nZoneNames, zoneNames);
}

void mainscreenPresenter::onWarningStatusChanged(bool active, uint8_t nItems, char (*bigTitles)[WARNING_TITLE_LEN],
						 char (*details)[ZONE_NAME_SIZE + 1])
{
	view.updateWarningStatus(active, nItems, bigTitles, details);
}

void mainscreenPresenter::onSoundOnChanged(bool soundOn)
{
	view.applyMuteIcon(soundOn);
}

void mainscreenPresenter::onWifiLinkChanged(bool active)
{
	view.applyWifiIcon(active);
}
#endif
