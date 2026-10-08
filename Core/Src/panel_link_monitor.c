#include "panel_link_monitor.h"

#include "rs_panel_debug.h"
#include "rs_panel_endpoint.h"
#include "rs_panel_v3_slave.h"
#include "led.h"
#include "menu_ui.h"
#include "device_config.h"
#include "main.h"

#include <string.h>
#include <stdio.h>

static uint32_t s_boot_ms;
static uint8_t s_boot_valid;
static uint8_t s_ever_linked;
static uint8_t s_lost_active;

static void panel_link_push_lost_ui(void)
{
	char titles[16][24];
	char details[16][ZONE_NAME_SIZE + 1];

	memset(titles, 0, sizeof(titles));
	memset(details, 0, sizeof(details));
	(void)snprintf(titles[0], sizeof(titles[0]), "НЕТ СВЯЗИ");
	(void)snprintf(details[0], sizeof(details[0]), "НЕТ СВЯЗИ С МОДУЛЕМ ППКУ");

	RsPanelEndpoint_QueueWarningUi(1u, 1u, titles, details);
	/* С меню — на MAIN, чтобы АВАРИЯ/центр были видны. */
	if (MenuUi_IsMainScreenActive() == 0u || MenuUi_GetMenuSessionScreen() != 0u) {
		RsPanelEndpoint_QueueGotoMain();
	}

	Led_Set(LED_NORM, 0u);
	Led_Set(LED_ERR, 1u);
	Led_ForceStatusBright(LED_ERR);
}

static void panel_link_clear_lost_ui(void)
{
	if (RsPanelV3Slave_IsV3Active() != 0u) {
		RsPanelV3Slave_RequestFaultUiRefresh();
	} else {
		RsPanelEndpoint_QueueWarningUi(0u, 0u, 0, 0);
	}
}

void PanelLinkMonitor_Init(void)
{
	s_boot_ms = HAL_GetTick();
	s_boot_valid = 1u;
	s_ever_linked = 0u;
	s_lost_active = 0u;
}

uint8_t PanelLinkMonitor_IsLost(void)
{
	return s_lost_active;
}

void PanelLinkMonitor_Timer10ms(void)
{
	uint32_t now = HAL_GetTick();
	uint8_t lost = 0u;

	if (s_boot_valid == 0u) {
		s_boot_ms = now;
		s_boot_valid = 1u;
	}

	if (g_rs_panel_dbg.last_host_rx_ms != 0u) {
		s_ever_linked = 1u;
	}

	if (s_ever_linked != 0u) {
		if ((now - g_rs_panel_dbg.last_host_rx_ms) >= PANEL_LINK_LOST_AFTER_OK_MS) {
			lost = 1u;
		}
	} else if ((now - s_boot_ms) >= PANEL_LINK_NEVER_BOOT_MS) {
		lost = 1u;
	}

	if (lost != 0u) {
		if (s_lost_active == 0u) {
			s_lost_active = 1u;
			panel_link_push_lost_ui();
		}
	} else if (s_lost_active != 0u) {
		s_lost_active = 0u;
		panel_link_clear_lost_ui();
	}
}
