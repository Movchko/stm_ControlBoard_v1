#ifndef PANEL_JOURNAL_CACHE_H
#define PANEL_JOURNAL_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "event_logger.h"

#ifdef __cplusplus
extern "C" {
#endif

void PanelJournalCache_SetList(uint32_t total,
                               uint32_t selected_idx,
                               uint32_t window_first,
                               uint8_t n_items,
                               const uint8_t *items,
                               uint16_t items_len);

void PanelJournalCache_SetDetail(uint32_t rec_idx,
                                 uint32_t ts,
                                 uint16_t code,
                                 const uint8_t *text,
                                 uint16_t text_len);

/** Только total (ответ JOURNAL_COUNT) — не затирать уже принятое окно. */
void PanelJournalCache_SetTotal(uint32_t total);

uint32_t PanelJournalCache_GetCapacity(void);
uint32_t PanelJournalCache_GetCount(void);
uint32_t PanelJournalCache_GetSelected(void);

/** 1 = кэш обновился с прошлого Take; для refresh UI в TouchGFX tick. */
uint8_t PanelJournalCache_TakeDirty(void);

bool PanelJournalCache_ReadRecord(uint32_t logical_index, EventLogRecord_t *out_record);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_JOURNAL_CACHE_H */
