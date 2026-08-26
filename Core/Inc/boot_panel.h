#ifndef BOOT_PANEL_H_
#define BOOT_PANEL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Контракт flash/SRAM между бутлоадером v2 и приложением панели.
 * Дублируется в MCU_bootloader_v2/Core/Inc/boot_panel.h — держать синхронно.
 *
 * Boot: 40 КБ (сектора 0..4), код линкуется в LENGTH=40K-16; последние 16 байт — boot WD.
 * App WD: 16 байт сразу перед футером (тот же сектор, что футер) — стирается вместе с образом.
 */
#define PANEL_FLASH_BASE            0x08000000u
#define PANEL_BOOTLOADER_SIZE       0xA000u          /* 40 КБ, сектора 0..4 */
#define PANEL_FOOTER_SECTOR_ADDR    0x0800C000u      /* сектор 6 */
#define PANEL_APP_START_ADDR        0x0800E000u      /* сектор 7 */
#define PANEL_APP_SIZE_SECTORS      28u
#define PANEL_APP_SIZE_BYTES        (PANEL_APP_SIZE_SECTORS * 8u * 1024u)
#define PANEL_FLASH_CFG_ADDR        0x0807E000u
#define PANEL_FLASH_CFG_SIZE        0x2000u
#define PANEL_FLASH_FOOTER_SZ       64u
#define PANEL_APP_FOOTER_ADDR       (PANEL_APP_START_ADDR - PANEL_FLASH_FOOTER_SZ)

/* Boot WD: конец области бутлоадера (линкер: FLASH LENGTH = 40K - 16). */
#define PANEL_BOOT_KEY_ADDR         (PANEL_FLASH_BASE + PANEL_BOOTLOADER_SIZE - 16u)
/* App WD: перед футером, в секторе футера/образа (EraseApp стирает). */
#define PANEL_APP_WD_ADDR           (PANEL_APP_FOOTER_ADDR - 16u)

#define PANEL_WATCHDOG_MAGIC        0xAABBCCDDu

#define PANEL_BOOT_SRAM_FLAGS_ADDR  0x20043E00u
#define PANEL_BOOT_UPD_REQ_MAGIC      0x55504452u /* 'U','P','D','R' */
#define PANEL_BOOT_JUST_UPDATED_MAGIC 0x4A555044u /* 'J','U','P','D' */

#define PANEL_DEFAULT_RS_ADDR       0x01u

typedef struct {
	uint32_t update_request; /* PANEL_BOOT_UPD_REQ_MAGIC */
	uint32_t just_updated;   /* PANEL_BOOT_JUST_UPDATED_MAGIC */
	uint32_t rs_addr;        /* 0x01..0xFE — тот же адрес, что у приложения панели */
} PanelBootSramFlags;

static inline PanelBootSramFlags *PanelBoot_SramFlags(void)
{
	return (PanelBootSramFlags *)(uintptr_t)PANEL_BOOT_SRAM_FLAGS_ADDR;
}

static inline uint8_t PanelBoot_IsValidRsAddr(uint8_t addr)
{
	return (addr >= 0x01u && addr <= 0xFEu) ? 1u : 0u;
}

static inline uint8_t PanelBoot_GetRsAddr(void)
{
	uint8_t addr = (uint8_t)(PanelBoot_SramFlags()->rs_addr & 0xFFu);
	return PanelBoot_IsValidRsAddr(addr) ? addr : PANEL_DEFAULT_RS_ADDR;
}

static inline void PanelBoot_SetRsAddr(uint8_t addr)
{
	if (PanelBoot_IsValidRsAddr(addr) != 0u) {
		PanelBoot_SramFlags()->rs_addr = (uint32_t)addr;
	}
}

static inline void PanelBoot_SetUpdateRequest(uint8_t rs_addr)
{
	PanelBootSramFlags *f = PanelBoot_SramFlags();
	PanelBoot_SetRsAddr(rs_addr);
	f->update_request = PANEL_BOOT_UPD_REQ_MAGIC;
	f->just_updated = 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* BOOT_PANEL_H_ */
