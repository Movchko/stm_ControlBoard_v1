/*
 * upd.cpp
 *
 * Версия приложения панели и всё, что связано с обновлением:
 * - строка/число версии (cmd 159 / CAPS.fw_ver);
 * - вход в бутлоадер по запросу хоста (TAMP handoff + reset);
 * - запись app-watchdog после успешного старта нового образа.
 */
#include "main.h"
#include "backend.h"
#include "boot_panel.h"
#include "upd.h"

#include <stdio.h>

/* fw_updater / stm_loader читают это имя из файла. */
#define APP_VERSION_U32 6u

static volatile uint8_t g_enter_bootloader_pending = 0u;

extern "C" uint32_t GetAppVersionU32(void)
{
	return (uint32_t)APP_VERSION_U32;
}

const char *GetAppVersion(void)
{
	static char ver_buf[48];
	(void)snprintf(ver_buf, sizeof(ver_buf), "fw=%u", (unsigned)APP_VERSION_U32);
	return ver_buf;
}

void PanelUpd_RequestEnterBootloader(uint8_t rs_addr)
{
	PanelBoot_SetUpdateRequest(rs_addr);
	g_enter_bootloader_pending = 1u;
}

void PanelUpd_Timer10ms(void)
{
	/* Reset не из UART RX IRQ: ACK уже ушёл, сброс — из 10 мс тика. */
	if (g_enter_bootloader_pending != 0u) {
		g_enter_bootloader_pending = 0u;
		NVIC_SystemReset();
	}
}

void App_WriteProgramWatchdog(void)
{
	//TODO delete return
	return;

	uint32_t *val = (uint32_t *)PANEL_APP_WD_ADDR;
	uint32_t quad_word[4];

	if (*val == PANEL_WATCHDOG_MAGIC) {
		return;
	}

	quad_word[0] = PANEL_WATCHDOG_MAGIC;
	quad_word[1] = 0xFFFFFFFFu;
	quad_word[2] = 0xFFFFFFFFu;
	quad_word[3] = 0xFFFFFFFFu;

	(void)HAL_ICACHE_Disable();
	HAL_FLASH_Unlock();
	(void)HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, PANEL_APP_WD_ADDR, (uint32_t)quad_word);
	HAL_FLASH_Lock();
	(void)HAL_ICACHE_Invalidate();
	(void)HAL_ICACHE_Enable();
}
