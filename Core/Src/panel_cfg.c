#include "panel_cfg.h"

#include <string.h>

#include "boot_panel.h"
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_flash.h"
#include "stm32h5xx_hal_flash_ex.h"

#define PANEL_CFG_HEADER_SIZE 8u
#define QUADWORD_SIZE         16u
#define FLASH_CFG_SECTOR      31u

DevicePanelConfig g_panel_cfg;

void PanelCfg_RefreshChipUid(DevicePanelConfig *cfg)
{
	if (cfg == 0) {
		return;
	}
	cfg->uid0 = HAL_GetUIDw0();
	cfg->uid1 = HAL_GetUIDw1();
	cfg->uid2 = HAL_GetUIDw2();
}

void PanelCfg_SetDefaults(DevicePanelConfig *cfg)
{
	if (cfg == 0) {
		return;
	}
	memset(cfg, 0, sizeof(*cfg));
	cfg->rs_addr = PANEL_DEFAULT_RS_ADDR;
	cfg->orientation = 0u;
	cfg->journal_lines = 1u;
	cfg->btn_enable = 0xFFu;
	cfg->led_enable = 0xFFFFu;
	cfg->role = 0u;
	cfg->addr_assigned = 0u;
	PanelCfg_RefreshChipUid(cfg);
}

uint8_t PanelCfg_IsValidRsAddr(uint8_t addr)
{
	return PanelBoot_IsValidRsAddr(addr);
}

uint8_t PanelCfg_IsVirgin(void)
{
	return (g_panel_cfg.addr_assigned == 0u) ? 1u : 0u;
}

uint16_t PanelCfg_DiscoverDelayMs(void)
{
	/* 20..520 мс: разнос ответов virgin-панелей на DISCOVER. */
	uint32_t mix = g_panel_cfg.uid0 ^ (g_panel_cfg.uid1 << 1) ^ g_panel_cfg.uid2;
	return (uint16_t)(20u + (mix % 51u) * 10u);
}

uint8_t PanelCfg_FlashRead(DevicePanelConfig *out)
{
	const uint32_t *p;

	if (out == 0) {
		return 0u;
	}
	p = (const uint32_t *)PANEL_FLASH_CFG_ADDR_SYM;
	if (p[0] != PANEL_CFG_HEADER_MAGIC) {
		return 0u;
	}
	if (p[1] != (uint32_t)sizeof(DevicePanelConfig) ||
	    p[1] > (PANEL_FLASH_CFG_SIZE_SYM - PANEL_CFG_HEADER_SIZE)) {
		return 0u;
	}
	memcpy(out, p + 2, sizeof(DevicePanelConfig));
	if (PanelCfg_IsValidRsAddr(out->rs_addr) == 0u) {
		return 0u;
	}
	if (out->journal_lines == 0u) {
		out->journal_lines = 1u;
	}
	if (out->addr_assigned > 1u) {
		out->addr_assigned = 0u;
	}
	PanelCfg_RefreshChipUid(out);
	return 1u;
}

void PanelCfg_FlashWrite(const DevicePanelConfig *cfg)
{
	__attribute__((aligned(16))) uint8_t buf[PANEL_FLASH_CFG_BYTES];
	uint32_t *hdr;
	uint32_t total;
	uint32_t n_quad;
	FLASH_EraseInitTypeDef erase;
	uint32_t sector_err = 0u;
	HAL_StatusTypeDef st;
	uint32_t prog_type;
	uint32_t i;

	if (cfg == 0 || sizeof(DevicePanelConfig) > (PANEL_FLASH_CFG_BYTES - PANEL_CFG_HEADER_SIZE)) {
		return;
	}

	memset(buf, 0xFFu, sizeof(buf));
	hdr = (uint32_t *)buf;
	hdr[0] = PANEL_CFG_HEADER_MAGIC;
	hdr[1] = (uint32_t)sizeof(DevicePanelConfig);
	memcpy(buf + PANEL_CFG_HEADER_SIZE, cfg, sizeof(DevicePanelConfig));

	total = PANEL_CFG_HEADER_SIZE + (uint32_t)sizeof(DevicePanelConfig);
	n_quad = (total + QUADWORD_SIZE - 1u) / QUADWORD_SIZE;

#if defined(FLASH_TYPEERASE_SECTORS_NS)
	erase.TypeErase = FLASH_TYPEERASE_SECTORS_NS;
#else
	erase.TypeErase = FLASH_TYPEERASE_SECTORS;
#endif
	erase.Banks = FLASH_BANK_2;
	erase.Sector = FLASH_CFG_SECTOR;
	erase.NbSectors = 1u;

	st = HAL_FLASH_Unlock();
	if (st != HAL_OK) {
		return;
	}
	st = HAL_FLASHEx_Erase(&erase, &sector_err);
	if (st != HAL_OK) {
		(void)HAL_FLASH_Lock();
		return;
	}
#if defined(FLASH_TYPEPROGRAM_QUADWORD_NS)
	prog_type = FLASH_TYPEPROGRAM_QUADWORD_NS;
#else
	prog_type = FLASH_TYPEPROGRAM_QUADWORD;
#endif
	for (i = 0u; i < n_quad && st == HAL_OK; i++) {
		uint32_t addr = PANEL_FLASH_CFG_ADDR_SYM + i * QUADWORD_SIZE;
		st = HAL_FLASH_Program(prog_type, addr, (uint32_t)(buf + i * QUADWORD_SIZE));
	}
	(void)HAL_FLASH_Lock();
}

void PanelCfg_Save(void)
{
	PanelCfg_RefreshChipUid(&g_panel_cfg);
	PanelCfg_FlashWrite(&g_panel_cfg);
}

uint8_t PanelCfg_Init(void)
{
	if (PanelCfg_FlashRead(&g_panel_cfg) == 0u) {
		PanelCfg_SetDefaults(&g_panel_cfg);
		PanelCfg_Save();
	} else {
		PanelCfg_RefreshChipUid(&g_panel_cfg);
	}
	return g_panel_cfg.rs_addr;
}

const DevicePanelConfig *PanelCfg_Get(void)
{
	return &g_panel_cfg;
}
