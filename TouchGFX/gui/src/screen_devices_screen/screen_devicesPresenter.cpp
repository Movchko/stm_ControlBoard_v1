#include <gui/screen_devices_screen/screen_devicesView.hpp>

#include <gui/screen_devices_screen/screen_devicesPresenter.hpp>

#include <gui/common/FrontendApplication.hpp>

#include <touchgfx/Application.hpp>

#ifndef SIMULATOR

#include "button.h"

#include "menu_ui.h"

#endif



screen_devicesPresenter::screen_devicesPresenter(screen_devicesView& v)

    : view(v)

{



}



void screen_devicesPresenter::activate()

{

#ifndef SIMULATOR

    view.refreshDeviceUi();

#endif

}



void screen_devicesPresenter::deactivate()

{



}



#ifndef SIMULATOR

void screen_devicesPresenter::handleButton(uint8_t but, uint8_t state)

{
    (void)but;
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }
    /* В режиме RS-контроля выбор/открытие устройства обрабатывает master
     * через panel_state -> UI events. */
}

void screen_devicesPresenter::onAppTick()
{
    view.refreshDeviceUi();
}

#endif

