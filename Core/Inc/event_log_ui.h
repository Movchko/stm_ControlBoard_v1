/*
 * event_log_ui.h — форматирование записи журнала для OLED (128×64).
 */

#ifndef INC_EVENT_LOG_UI_H_
#define INC_EVENT_LOG_UI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "event_logger.h"

#define EVENT_LOG_UI_HEADER_LEN  32u
#define EVENT_LOG_UI_TITLE_LEN   24u
#define EVENT_LOG_UI_DETAIL_LEN  384u

typedef struct {
	char header[EVENT_LOG_UI_HEADER_LEN]; /* DD.MM.YY HH:MM n/N (верхняя бегущая строка) */
	char title[EVENT_LOG_UI_TITLE_LEN];   /* Large, ≤10–11 символов */
	char detail[EVENT_LOG_UI_DETAIL_LEN]; /* бегущая строка: детали события */
} EventLogUiLines_t;

/** tier критических событий для UI журнала. */
#define EVENT_LOG_UI_TIER  0u

/**
 * Заполнить строки UI по записи.
 * @param display_index_1based  позиция 1..count для отображения
 * @param count                 число валидных записей в tier (для «n/N»)
 */
void EventLogUi_FormatRecord(const EventLogRecord_t *rec,
			     uint32_t display_index_1based,
			     uint32_t count,
			     EventLogUiLines_t *out);

/** Пустой журнал / нет валидных записей. */
void EventLogUi_FormatEmpty(EventLogUiLines_t *out);

#ifdef __cplusplus
}
#endif

#endif /* INC_EVENT_LOG_UI_H_ */
