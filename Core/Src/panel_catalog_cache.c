#include "panel_catalog_cache.h"

#include <string.h>

#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_flash.h"
#include "stm32h5xx_hal_flash_ex.h"

#define PANEL_CATALOG_HEADER_SIZE 8u
#define PANEL_CATALOG_MAGIC       0x50434C47u /* 'PCLG' */
#define QUADWORD_SIZE             16u
#define FLASH_CATALOG_SECTOR      30u /* Bank2: 0x0807C000 */

uint8_t PanelCatalog_FlashRead(PanelCatalogBlob *out)
{
	const uint32_t *p;

	if (out == 0) {
		return 0u;
	}
	p = (const uint32_t *)PANEL_FLASH_CATALOG_ADDR_SYM;
	if (p[0] != PANEL_CATALOG_MAGIC) {
		return 0u;
	}
	if (p[1] != (uint32_t)sizeof(PanelCatalogBlob) ||
	    p[1] > (PANEL_FLASH_CATALOG_SIZE_SYM - PANEL_CATALOG_HEADER_SIZE)) {
		return 0u;
	}
	memcpy(out, p + 2, sizeof(PanelCatalogBlob));
	if (out->device_count > RS_PANEL_V3_MAX_CATALOG_MCU) {
		return 0u;
	}
	return 1u;
}

void PanelCatalog_FlashWrite(const PanelCatalogBlob *blob)
{
	/* НЕ на стеке: PANEL_FLASH_CATALOG_BYTES=8K, а _Min_Stack_Size=4K.
	 * После сходимости каталога quiet-save ронял стек → паузы OnRsp на шине. */
	static __attribute__((aligned(16))) uint8_t s_flash_buf[PANEL_FLASH_CATALOG_BYTES];
	uint32_t *hdr;
	uint32_t total;
	uint32_t n_quad;
	FLASH_EraseInitTypeDef erase;
	uint32_t sector_err = 0u;
	HAL_StatusTypeDef st;
	uint32_t prog_type;
	uint32_t i;

	if (blob == 0 ||
	    sizeof(PanelCatalogBlob) > (PANEL_FLASH_CATALOG_BYTES - PANEL_CATALOG_HEADER_SIZE)) {
		return;
	}

	memset(s_flash_buf, 0xFFu, sizeof(s_flash_buf));
	hdr = (uint32_t *)s_flash_buf;
	hdr[0] = PANEL_CATALOG_MAGIC;
	hdr[1] = (uint32_t)sizeof(PanelCatalogBlob);
	memcpy(s_flash_buf + PANEL_CATALOG_HEADER_SIZE, blob, sizeof(PanelCatalogBlob));

	total = PANEL_CATALOG_HEADER_SIZE + (uint32_t)sizeof(PanelCatalogBlob);
	n_quad = (total + QUADWORD_SIZE - 1u) / QUADWORD_SIZE;

#if defined(FLASH_TYPEERASE_SECTORS_NS)
	erase.TypeErase = FLASH_TYPEERASE_SECTORS_NS;
#else
	erase.TypeErase = FLASH_TYPEERASE_SECTORS;
#endif
	erase.Banks = FLASH_BANK_2;
	erase.Sector = FLASH_CATALOG_SECTOR;
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
		uint32_t addr = PANEL_FLASH_CATALOG_ADDR_SYM + i * QUADWORD_SIZE;
		st = HAL_FLASH_Program(prog_type, addr, (uint32_t)(s_flash_buf + i * QUADWORD_SIZE));
	}
	(void)HAL_FLASH_Lock();
}
