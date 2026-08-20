#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalView.hpp>
#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#endif

ScreenMenu_jurnalPresenter::ScreenMenu_jurnalPresenter(ScreenMenu_jurnalView& v)
    : view(v)
{
}

void ScreenMenu_jurnalPresenter::activate()
{
#ifndef SIMULATOR
    view.refreshJournalUi();
#endif
}

void ScreenMenu_jurnalPresenter::deactivate()
{
}

#ifndef SIMULATOR
void ScreenMenu_jurnalPresenter::handleButton(uint8_t but, uint8_t state)
{
    (void)but;
    (void)state;
    /* В RS-режиме навигация журнала идёт через ui_events -> master -> UI_DATA/UI_NAV.
     * Локально здесь кнопки не обрабатываем, чтобы не было рассинхронизации. */
}
#endif
