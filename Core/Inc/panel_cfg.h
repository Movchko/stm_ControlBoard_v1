/**
 * @file panel_cfg.h
 * @brief Конфиг панели (DevicePanelConfig) во Flash — секция .mku_cfg / FLASH_CFG.
 */
#ifndef PANEL_CFG_H
#define PANEL_CFG_H

#include <stdint.h>
#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t _mku_cfg_start[];
extern uint8_t _mku_cfg_end[];

#define PANEL_FLASH_CFG_ADDR_SYM ((uint32_t)_mku_cfg_start)
#define PANEL_FLASH_CFG_SIZE_SYM ((uint32_t)(_mku_cfg_end - _mku_cfg_start))
#define PANEL_FLASH_CFG_BYTES    0x2000u

/* RAM-копия конфига панели (загружается при Init). */
extern DevicePanelConfig g_panel_cfg;

void PanelCfg_SetDefaults(DevicePanelConfig *cfg);
void PanelCfg_RefreshChipUid(DevicePanelConfig *cfg);
uint8_t PanelCfg_IsValidRsAddr(uint8_t addr);
uint8_t PanelCfg_IsVirgin(void);
/* Читает Flash; 1=ok, 0=нет валидной записи. */
uint8_t PanelCfg_FlashRead(DevicePanelConfig *out);
void PanelCfg_FlashWrite(const DevicePanelConfig *cfg);
void PanelCfg_Save(void);
/* Load Flash или defaults+Save; UID всегда с чипа. Возвращает rs_addr. */
uint8_t PanelCfg_Init(void);
const DevicePanelConfig *PanelCfg_Get(void);
/* Задержка ответа на DISCOVER (мс) по UID — разнос коллизий. */
uint16_t PanelCfg_DiscoverDelayMs(void);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_CFG_H */
