/**
 * @file panel_host_cache.h
 * @brief RAM-кэш данных ППКУ для UI панели.
 *
 * Не путать с DevicePanelConfig (Flash панели) и с полной PPKYCfg хоста:
 * здесь только поля, которые реально читает/пишет панель.
 */
#ifndef PANEL_HOST_CACHE_H
#define PANEL_HOST_CACHE_H

#include <stdint.h>
#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t zone;
    uint8_t l_adr;
    uint8_t h_adr;
    uint8_t d_type;
    uint32_t uid0;
    uint32_t uid1;
    uint32_t uid2;
} PanelHostDevice;

typedef struct {
    uint8_t beep;
    uint8_t beep_block;
    uint8_t fire_mode;
    uint8_t rs485_on;
    /* UID ППКУ (fallback для SN; обычно приходит в FAULT_EVT.mcu). */
    uint32_t host_uid0;
    uint32_t host_uid1;
    uint32_t host_uid2;
    PanelHostDevice devices[MAX_MCU_IN_BUS];
    int8_t zone_name[ZONE_NUMBER][ZONE_NAME_SIZE];
    uint8_t zone_fire_mode[ZONE_NUMBER];
} PanelHostCache;

void PanelHostCache_Init(void);
PanelHostCache *PanelHostCache_Get(void);
const PanelHostCache *PanelHostCache_GetConst(void);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_HOST_CACHE_H */
