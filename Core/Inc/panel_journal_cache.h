#ifndef PANEL_JOURNAL_CACHE_H
#define PANEL_JOURNAL_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "event_logger.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Буфер журнала: до WINDOW записей вокруг selected.
 * У края (новейшая) — последние WINDOW шт.; в середине — selected ± MARGIN.
 */
#define PANEL_JOURNAL_MARGIN 5u
#define PANEL_JOURNAL_WINDOW (1u + (2u * PANEL_JOURNAL_MARGIN)) /* 11 */
/** Догрузка при листании / заполнении дыр (согласовано с ППКУ RS_V3_JOURNAL_BATCH). */
#define PANEL_JOURNAL_PREFETCH_N 3u

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

/**
 * SYS_READY 0→1: фоновая загрузка последних WINDOW событий (GET + GET_N×3).
 * Open: сразу кэш (если есть), затем JOURNAL_GET newest — сверка total;
 * при расхождении сброс окна и догрузка пула. Navigate — локально + GET_N×3.
 */
void PanelJournalCache_OnSysReady(void);
void PanelJournalCache_Open(void);
/** dir: 0=новее (UP), 1=старее (DOWN). Сразу двигает selected, если есть куда. */
void PanelJournalCache_Navigate(uint8_t dir);
void PanelJournalCache_JumpNewest(void);
/** Вызывать из Drain / OnPoll / Timer — шлёт следующий RS-запрос, если слот свободен. */
void PanelJournalCache_PrefetchProcess(void);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_JOURNAL_CACHE_H */
