#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalView.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstring>
#include <cstdio>

#ifndef SIMULATOR
#include "event_log_reader.h"
#include "event_log_ui.h"
#include "event_logger.h"
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
        /* Стартуем с новейшей (logical = count-1). */
        uint32_t start = recordCount - 1u;
        if (!loadLogical(start)) {
            /* Ищем ближайшую валидную назад. */
            logicalIndex = start;
            hasValid = 0u;
            for (uint32_t i = 0u; i < recordCount && i < 64u; i++) {
                uint32_t idx = start - i;
                if (loadLogical(idx)) {
                    break;
                }
                if (idx == 0u) {
                    break;
                }
            }
        }
    }

    renderCurrent();
}

void ScreenMenu_jurnalView::nextRecord()
{
    if (stepValid(+1)) {
        renderCurrent();
    }
}

void ScreenMenu_jurnalView::prevRecord()
{
    if (stepValid(-1)) {
        renderCurrent();
    }
}

void ScreenMenu_jurnalView::jumpToNewest()
{
    if (recordCount == 0u) {
        return;
    }
    uint32_t start = recordCount - 1u;
    if (loadLogical(start)) {
        renderCurrent();
        return;
    }
    for (uint32_t i = 1u; i < recordCount && i < 64u; i++) {
        uint32_t idx = start - i;
        if (loadLogical(idx)) {
            renderCurrent();
            return;
        }
        if (idx == 0u) {
            break;
        }
    }
}
#endif
