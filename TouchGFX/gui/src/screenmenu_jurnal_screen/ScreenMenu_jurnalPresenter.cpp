#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalView.hpp>
#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalPresenter.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/Application.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "panel_ui_bridge.h"
#include "panel_journal_cache.h"
#include "rs_panel_protocol.h"
#include "rs_panel_v3_slave.h"
#endif

ScreenMenu_jurnalPresenter::ScreenMenu_jurnalPresenter(ScreenMenu_jurnalView& v)
    : view(v)
{
}

void ScreenMenu_jurnalPresenter::activate()
{
#ifndef SIMULATOR
    /* Запросить новейшую запись (окно с ППКУ). Локальный кэш пуст до event_reply. */
    if (RsPanelV3Slave_IsV3Active() != 0u) {
        (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_JOURNAL_GET, 0u, 2u, 0u, 0u);
    }
    (void)PanelJournalCache_TakeDirty();
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
    /* UP/DOWN/ENTER → panel_state NAV/CONFIRM → Drain → JOURNAL_GET.
     * Локальный next/prev не трогаем: в кэше только окно (часто 1 запись). */
    if (but == BUT_UP || but == BUT_DOWN || but == BUT_ENTER) {
        return;
    }
}

void ScreenMenu_jurnalPresenter::onAppTick()
{
    if (PanelJournalCache_TakeDirty() != 0u) {
        view.refreshJournalUi();
    }
}
#endif
