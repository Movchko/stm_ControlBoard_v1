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
    if (state != (uint8_t)ButtonStatePress) {
        return;
    }

    FrontendApplication* app = static_cast<FrontendApplication*>(touchgfx::Application::getInstance());

    if (but == BUT_ESC) {
        app->gotoScreenMenuScreenNoTransition();
        return;
    }
    if (but == BUT_DOWN) {
        /* Старее */
        view.prevRecord();
        return;
    }
    if (but == BUT_UP) {
        /* Новее */
        view.nextRecord();
        return;
    }
    if (but == BUT_ENTER) {
        view.jumpToNewest();
        return;
    }
}
#endif
