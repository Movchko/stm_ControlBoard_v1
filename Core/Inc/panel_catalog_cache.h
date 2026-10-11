/**
 * @file panel_catalog_cache.h
 * @brief Flash-кэш каталога v3 (имена зон + карта МКУ).
 *
 * Отдельно от DevicePanelConfig (panel_cfg): сохранение только по завершении
 * обновления каталога с хоста, не на каждый POLL.
 */
#ifndef PANEL_CATALOG_CACHE_H
#define PANEL_CATALOG_CACHE_H

#include <stdint.h>
#include "rs_panel_protocol_v3.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t _panel_catalog_start[];
extern uint8_t _panel_catalog_end[];

#define PANEL_FLASH_CATALOG_ADDR_SYM ((uint32_t)_panel_catalog_start)
#define PANEL_FLASH_CATALOG_SIZE_SYM ((uint32_t)(_panel_catalog_end - _panel_catalog_start))
#define PANEL_FLASH_CATALOG_BYTES    0x2000u

#define PANEL_CATALOG_ZONE_SLOTS     32u

typedef struct {
	uint8_t zone_valid[PANEL_CATALOG_ZONE_SLOTS]; /* index 0 = zone 1 */
	char zone_name[PANEL_CATALOG_ZONE_SLOTS][RS_PANEL_V3_ZONE_NAME_LEN];
	uint16_t device_count;
	RsPanelV3DeviceItem devices[RS_PANEL_V3_MAX_CATALOG_MCU];
	uint16_t zones_crc;
	uint16_t devices_crc;
} PanelCatalogBlob;

/** 1 = прочитан валидный blob из Flash. */
uint8_t PanelCatalog_FlashRead(PanelCatalogBlob *out);
void PanelCatalog_FlashWrite(const PanelCatalogBlob *blob);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_CATALOG_CACHE_H */
