#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalView.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstring>
#include <cstdio>

#ifndef SIMULATOR
#include "event_log_reader.h"
#include "event_log_ui.h"
#include "event_logger.h"
#include "panel_journal_cache.h"
#endif

ScreenMenu_jurnalView::ScreenMenu_jurnalView()
{
}

void ScreenMenu_jurnalView::setupScreen()
{
    ScreenMenu_jurnalViewBase::setupScreen();
#ifndef SIMULATOR
    refreshJournalUi();
#endif
}

void ScreenMenu_jurnalView::tearDownScreen()
{
    ScreenMenu_jurnalViewBase::tearDownScreen();
}

#ifndef SIMULATOR
void ScreenMenu_jurnalView::renderCurrent()
{
    EventLogUiLines_t lines;
    if (hasValid == 0u || recordCount == 0u) {
        EventLogUi_FormatEmpty(&lines);
    } else {
        EventLogRecord_t rec;
        EventLogRecStatus_t st = EVENT_LOG_REC_EMPTY;
        if (!EventLogReader_ReadLogical(EVENT_LOG_UI_TIER, logicalIndex, &st, &rec) ||
            st != EVENT_LOG_REC_VALID) {
            EventLogUi_FormatEmpty(&lines);
        } else {
            /* Позиция 1 = старейшая, count = новейшая. */
            uint32_t display_1based = logicalIndex + 1u;
            EventLogUi_FormatRecord(&rec, display_1based, recordCount, &lines);
        }
    }

    CustomContainerSrollText_1.setText(lines.header);

    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(lines.title),
                      textArea1Buffer, TEXTAREA1_SIZE);
    textArea1Buffer[TEXTAREA1_SIZE - 1] = 0;
    textArea1.setWildcard(textArea1Buffer);
    textArea1.invalidate();

    CustomContainerSrollText.setText(lines.detail);
}

bool ScreenMenu_jurnalView::loadLogical(uint32_t index)
{
    if (recordCount == 0u || index >= recordCount) {
        return false;
    }
    EventLogRecord_t rec;
    EventLogRecStatus_t st = EVENT_LOG_REC_EMPTY;
    if (!EventLogReader_ReadLogical(EVENT_LOG_UI_TIER, index, &st, &rec)) {
        return false;
    }
    if (st != EVENT_LOG_REC_VALID) {
        return false;
    }
    logicalIndex = index;
    hasValid = 1u;
    return true;
}

bool ScreenMenu_jurnalView::stepValid(int direction)
{
    if (recordCount == 0u) {
        return false;
    }
    uint32_t idx = logicalIndex;
    for (uint8_t n = 0u; n < 64u; n++) {
        if (direction < 0) {
            if (idx == 0u) {
                return false;
            }
            idx--;
        } else {
            if ((idx + 1u) >= recordCount) {
                return false;
            }
            idx++;
        }
        if (loadLogical(idx)) {
            return true;
        }
    }
    return false;
}

void ScreenMenu_jurnalView::refreshJournalUi()
{
    recordCount = 0u;
    logicalIndex = 0u;
    hasValid = 0u;

    EventLogTierInfo_t info;
    if (EventLogReader_GetTierInfo(EVENT_LOG_UI_TIER, &info) && info.count > 0u) {
        recordCount = info.count;
        /* Показать выбранную на ППКУ (окно в кэше); иначе новейшую, если есть в окне. */
        uint32_t start = PanelJournalCache_GetSelected();
        if (start >= recordCount) {
            start = recordCount - 1u;
        }
        if (!loadLogical(start)) {
            logicalIndex = start;
            hasValid = 0u;
            if (loadLogical(recordCount - 1u)) {
                /* ok */
            }
        }
    }

    renderCurrent();
}

void ScreenMenu_jurnalView::nextRecord()
{
    /* v3: листание через JOURNAL_GET (Drain), не локальный кэш. */
}

void ScreenMenu_jurnalView::prevRecord()
{
}

void ScreenMenu_jurnalView::jumpToNewest()
{
}
#endif
