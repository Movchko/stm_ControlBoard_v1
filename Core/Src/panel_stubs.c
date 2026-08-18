#include "fire.h"
#include "menu_ui.h"
#include "esp_manager.h"
#include "config_zone_block.h"
#include "config_ign_block_sync.h"
#include "event_log.h"
#include "event_log_ui.h"
#include "event_log_reader.h"
#include "device_config.h"
#include <string.h>

PPKYCfg PPKYConfig;

/* Конфиг хранится на ППКУ 2; на панели запись в Flash не делается. */
void SaveConfig(void) {}

/* --- fire: логика тушения остаётся на ППКУ 2 --- */
void Fire_Init(void) {}
void Fire_Timer1ms(void) {}
void Fire_Timer10ms(void) {}
void Fire_OnStatusFire(uint32_t msg_id, const uint8_t *msg_data) { (void)msg_id; (void)msg_data; }
void Fire_OnReplyStatusFire(uint32_t msg_id) { (void)msg_id; }
void Fire_OnStopExtinguishment(uint32_t msg_id) { (void)msg_id; }
void Fire_OnBusStartSpButton(uint32_t msg_id) { (void)msg_id; }
void Fire_OnStartExtinguishment(uint32_t msg_id, const uint8_t *msg_data) { (void)msg_id; (void)msg_data; }
void Fire_OnReplyStartExtinguishment(uint32_t msg_id) { (void)msg_id; }
void Fire_OnReplyStopExtinguishment(uint32_t msg_id) { (void)msg_id; }
void Fire_OnPauseExtinguishmentTimer(uint32_t msg_id) { (void)msg_id; }
void Fire_OnResumeExtinguishmentTimer(uint32_t msg_id) { (void)msg_id; }
void Fire_OnReplyPauseExtinguishmentTimer(uint32_t msg_id) { (void)msg_id; }
void Fire_OnReplyResumeExtinguishmentTimer(uint32_t msg_id) { (void)msg_id; }
uint8_t Fire_IsActive(void) { return 0u; }
uint8_t Fire_HasExtinguishIncomplete(void) { return 0u; }
uint8_t Fire_IsStartAllHoldActive(void) { return 0u; }
void Fire_UiSetManualSelection(uint8_t enabled, uint8_t selected_ui_index)
{
	(void)enabled;
	(void)selected_ui_index;
}
void Fire_NotifyZoneModeChanged(void) {}

/* --- menu / ESP32: WiFi на ППКУ 2 --- */
static uint8_t s_menu_cfg;
static uint8_t s_menu_main;
static uint8_t s_mcu_slot;

void MenuUi_SetConfigSession(uint8_t active) { s_menu_cfg = active; }
uint8_t MenuUi_IsConfigSessionActive(void) { return s_menu_cfg; }
void MenuUi_SetMainScreenActive(uint8_t active) { s_menu_main = active; }
uint8_t MenuUi_IsMainScreenActive(void) { return s_menu_main; }
void Esp32_SetEnabled(uint8_t enabled) { (void)enabled; }
uint8_t Esp32_IsEnabled(void) { return 0u; }
void MenuConfig_Reset(void) {}
void MenuConfig_OnWordReceived(uint16_t word_num) { (void)word_num; }
void MenuConfig_OnSaveCompleted(void) {}
void MenuConfig_OnApplySuccess(void) {}
MenuCfgState MenuConfig_GetState(void) { return MENU_CFG_STATE_IDLE; }
uint8_t MenuConfig_GetPercent(void) { return 0u; }
void MenuUi_SetMcuDetailSlot(uint8_t cfg_slot) { s_mcu_slot = cfg_slot; }
uint8_t MenuUi_GetMcuDetailSlot(void) { return s_mcu_slot; }

void EspManager_Init(void) {}
void EspManager_Process(uint32_t now_ms) { (void)now_ms; }
void EspManager_OnEspPoweredOn(void) {}
void EspManager_OnEspPoweredOff(void) {}
void EspManager_OnActivity(const uint8_t *payload, uint16_t len) { (void)payload; (void)len; }
void EspManager_RequestWifiEnable(void) {}
uint8_t EspManager_IsOnline(void) { return 0u; }
uint8_t EspManager_IsWifiEnabled(void) { return 0u; }
uint8_t EspManager_IsHostConnected(void) { return 0u; }
uint8_t EspManager_IsLinkActive(void) { return 0u; }

uint8_t PPKY_ZoneFireModeGet(uint8_t zone_idx) { (void)zone_idx; return 0u; }
void PPKY_ZoneFireModeSet(uint8_t zone_idx, uint8_t mode) { (void)zone_idx; (void)mode; }
uint8_t PPKY_ZoneEffectiveMode(uint8_t zone_can) { (void)zone_can; return 0u; }
uint8_t PPKY_ZoneLaunchBlockedByCanZone(uint8_t zone_can) { (void)zone_can; return 0u; }
uint8_t PPKY_ZoneIsManualByCanZone(uint8_t zone_can) { (void)zone_can; return 0u; }
uint8_t PPKY_AnyZoneManualOrBlocked(void) { return 0u; }
void PPKY_ZoneFireModeInitFromGlobal(void) {}
void PPKY_ZoneModeUiNotify(uint8_t zone_idx) { (void)zone_idx; }
uint8_t PPKY_ZoneModeUiConsumeNew(uint8_t *zone_idx_out)
{
	(void)zone_idx_out;
	return 0u;
}

void ConfigIgnBlockSync_Init(void) {}
void ConfigIgnBlockSync_Process1ms(uint32_t now_ms) { (void)now_ms; }
void ConfigIgnBlockSync_Request(void) {}
uint8_t ConfigIgnBlockSync_ShouldDeferCrc(void) { return 0u; }

void EventLog_LogSoundToggle(uint8_t enabled, uint8_t source) { (void)enabled; (void)source; }
void EventLog_LogFireModeChange(uint8_t mode, uint8_t source) { (void)mode; (void)source; }

void EventLogUi_FormatRecord(const EventLogRecord_t *rec,
			     uint32_t display_index_1based,
			     uint32_t count,
			     EventLogUiLines_t *out)
{
	(void)rec;
	(void)display_index_1based;
	(void)count;
	if (out == 0) {
		return;
	}
	EventLogUi_FormatEmpty(out);
}

void EventLogUi_FormatEmpty(EventLogUiLines_t *out)
{
	if (out == 0) {
		return;
	}
	memset(out, 0, sizeof(*out));
	(void)strncpy(out->header, "Журнал", sizeof(out->header) - 1u);
	(void)strncpy(out->title, "ПУСТО", sizeof(out->title) - 1u);
	(void)strncpy(out->detail, "Лог на ППКУ 2", sizeof(out->detail) - 1u);
}

bool EventLogReader_GetTierInfo(uint8_t tier, EventLogTierInfo_t *info)
{
	(void)tier;
	if (info == 0) {
		return false;
	}
	info->capacity = 0u;
	info->count = 0u;
	info->write_head = 0u;
	return true;
}

bool EventLogReader_ReadLogical(uint8_t tier,
				uint32_t logical_index,
				EventLogRecStatus_t *status,
				EventLogRecord_t *record)
{
	(void)tier;
	(void)logical_index;
	(void)record;
	if (status != 0) {
		*status = EVENT_LOG_REC_EMPTY;
	}
	return false;
}

uint32_t EventLogReader_GetCatalogCrc32(void)
{
	return 0u;
}
