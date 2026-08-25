#include "main.h"
#include "boot_panel.h"

/**
 * Сильная реализация для панели: записать app-watchdog один раз.
 * Вызывается из PanelApp_Timer10ms через 3 с после старта.
 */
void App_WriteProgramWatchdog(void)
{
	uint32_t *val = (uint32_t *)PANEL_APP_WD_ADDR;
	uint32_t quad_word[4];

	if (*val == PANEL_WATCHDOG_MAGIC) {
		return;
	}

	quad_word[0] = PANEL_WATCHDOG_MAGIC;
	quad_word[1] = 0xFFFFFFFFu;
	quad_word[2] = 0xFFFFFFFFu;
	quad_word[3] = 0xFFFFFFFFu;

	HAL_FLASH_Unlock();
	(void)HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, PANEL_APP_WD_ADDR, (uint32_t)quad_word);
	HAL_FLASH_Lock();
}
