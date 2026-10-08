#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalView.hpp>
#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "panel_ui_bridge.h"
#include "panel_journal_cache.h"
#include "rs_panel_v3_slave.h"
#endif

ScreenMenu_jurnalPresenter::ScreenMenu_jurnalPresenter(ScreenMenu_jurnalView& v)
    : view(v)
{
}

void ScreenMenu_jurnalPresenter::activate()
{
#ifndef SIMULATOR
    /* Буфер уже с READY (11 шт.); Open — к новейшей + догрузка дыр по 3. */
    if (RsPanelV3Slave_IsV3Active() != 0u) {
        PanelJournalCache_Open();
    }
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

    if (but == BUT_ESC) {
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_MENU_ROOT, RS_PANEL_UI_ACTION_REPLACE);
        return;
    }
    /* UP/DOWN/ENTER → panel_state → Drain → Navigate/Jump (локальный FIFO). */
    if (but == BUT_UP || but == BUT_DOWN || but == BUT_ENTER) {
        return;
    }
}

void ScreenMenu_jurnalPresenter::onAppTick()
{
    PanelJournalCache_PrefetchProcess();
    if (PanelJournalCache_TakeDirty() != 0u) {
        view.refreshJournalUi();
    }
}
#endif
