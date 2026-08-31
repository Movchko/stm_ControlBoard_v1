#include "fire.h"
#include "menu_ui.h"
#include "esp_manager.h"
#include "config_zone_block.h"
#include "config_ign_block_sync.h"
#include "event_log.h"
#include "event_log_ui.h"
#include "event_log_reader.h"
#include "device_config.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

PPKYCfg PPKYConfig;

/* ---------------- UI-driven fire state (RS v2 -> panel UI) ----------------
 * На панели логика пожара отсутствует, но TouchGFX/FrontendApplication
 * используют Fire_IsActive() и Fire_IsStartAllHoldActive() для приоритета
 * и принудительного перехода на главный экран.
 *
 * Эти флаги обновляются из PanelUiBridge_SetFireStatus() через
 * Fire_NotifyUiStatus() (см. panel_ui_bridge.cpp).
 */
static uint8_t s_ui_fire_active = 0u;          /* UI баннер “пожар/пуск удержание” активен */
static uint8_t s_fire_is_active = 0u;         /* аналог Fire_IsActive() из stm_PPKY v1 */
static uint8_t s_start_all_hold_active = 0u; /* аналог Fire_IsStartAllHoldActive() из stm_PPKY v1 */

void Fire_NotifyUiStatus(uint8_t ui_active, uint8_t mode, uint8_t remaining_s, uint8_t n_zones)
{
	(void)remaining_s;

	s_ui_fire_active = (ui_active != 0u) ? 1u : 0u;
	if (s_ui_fire_active == 0u) {
		s_fire_is_active = 0u;
		s_start_all_hold_active = 0u;
		return;
	}

	/* В stm_PPKY v1 удержание “ПУСК ОБЩИЙ” показывает 3-сек счётчик
	 * при g_fire.state==IDLE. В этот момент UI-режим = 1, а список зон = 0.
	 * Поэтому this-hold-idle можно отличить по (mode==1 && n_zones==0). */
	const uint8_t is_hold_idle = (mode == 1u && n_zones == 0u) ? 1u : 0u;

	/* Fire_IsActive() в v1 = 1 если FSM не IDLE или есть активные слоты зон.
	 * Для hold-idle FSM остаётся IDLE, значит Fire_IsActive должен быть 0. */
	s_fire_is_active = is_hold_idle ? 0u : 1u;

	/* Fire_IsStartAllHoldActive() в v1 = g_fire.all_hold_active.
	 * В UI обновлениях это состояние проявляется:
	 * - hold-idle: mode==1 && n_zones==0
	 * - hold при активном сценарии: mode==0 (ui_mode остаётся 0 в ветке all_hold_active<3s) */
	s_start_all_hold_active = (is_hold_idle != 0u || mode == 0u) ? 1u : 0u;
}

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
uint8_t Fire_IsActive(void) { return s_fire_is_active; }
uint8_t Fire_HasExtinguishIncomplete(void) { return 0u; }
uint8_t Fire_IsStartAllHoldActive(void) { return s_start_all_hold_active; }
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
static uint8_t s_block_zone_selected;
static uint8_t s_connection_selected;
static uint8_t s_wifi_blocked;
static uint16_t s_menu_selected;
static uint8_t s_menu_n_items;
static uint8_t s_menu_indication_test_req;
static uint8_t s_menu_fire_mode;
static uint8_t s_menu_sound_on;
static uint8_t s_menu_sound_blocked;
static MenuCfgState s_cfg_state = MENU_CFG_STATE_IDLE;
static uint8_t s_cfg_percent;
static uint32_t s_cfg_success_from_ms;
static uint8_t s_esp_enabled;
static uint8_t s_esp_online;
static uint8_t s_esp_host_connected;
static uint8_t s_esp_wifi_session;
static uint8_t s_user_wifi_on;

#define MENU_CFG_SUCCESS_HOLD_MS 5000u

void MenuUi_SetConfigSession(uint8_t active)
{
    s_menu_cfg = (active != 0u) ? 1u : 0u;
    if (s_menu_cfg == 0u) {
        s_cfg_state = MENU_CFG_STATE_IDLE;
        s_cfg_percent = 0u;
        s_cfg_success_from_ms = 0u;
    }
}
uint8_t MenuUi_IsConfigSessionActive(void) { return s_menu_cfg; }
uint8_t MenuUi_IsConfigOverlayActive(void)
{
    return (s_menu_cfg != 0u && s_cfg_state != MENU_CFG_STATE_IDLE) ? 1u : 0u;
}
void MenuUi_SetMainScreenActive(uint8_t active) { s_menu_main = active; }
uint8_t MenuUi_IsMainScreenActive(void) { return s_menu_main; }

void MenuUi_SetMenuIndex(int16_t index)
{
    if (index < 0) {
        index = 0;
    }
    s_menu_selected = (uint16_t)index;
}
int16_t MenuUi_GetMenuIndex(void) { return (int16_t)s_menu_selected; }
void MenuUi_ResetMenuIndex(void) { MenuUi_SetMenuSelected(0u); }

void Esp32_SetEnabled(uint8_t enabled) { s_esp_enabled = (enabled != 0u) ? 1u : 0u; }
uint8_t Esp32_IsEnabled(void) { return s_esp_enabled; }
void MenuConfig_Reset(void)
{
    s_cfg_state = MENU_CFG_STATE_RECEIVING;
    s_cfg_percent = 0u;
    s_cfg_success_from_ms = 0u;
}
void MenuConfig_OnWordReceived(uint16_t word_num) { (void)word_num; }
void MenuConfig_OnSaveCompleted(void) { (void)0; }
void MenuConfig_OnApplyStarted(void) { (void)0; }
void MenuConfig_OnApplySuccess(void)
{
    s_cfg_state = MENU_CFG_STATE_SUCCESS;
    s_cfg_percent = 100u;
    s_cfg_success_from_ms = HAL_GetTick();
}
MenuCfgState MenuConfig_GetState(void) { return s_cfg_state; }
uint8_t MenuConfig_GetPercent(void) { return s_cfg_percent; }
void MenuConfig_SetRemoteStatus(MenuCfgState state, uint8_t percent)
{
    s_cfg_state = state;
    s_cfg_percent = percent;
    if (state == MENU_CFG_STATE_SUCCESS) {
        s_cfg_success_from_ms = HAL_GetTick();
    } else if (state == MENU_CFG_STATE_IDLE) {
        s_cfg_success_from_ms = 0u;
    }
    if (state != MENU_CFG_STATE_IDLE) {
        s_menu_cfg = 1u;
    } else {
        s_menu_cfg = 0u;
    }
}
void MenuConfig_Process1ms(uint32_t now_ms)
{
    if (s_menu_cfg == 0u || s_cfg_state != MENU_CFG_STATE_SUCCESS) {
        return;
    }
    if (s_cfg_success_from_ms != 0u &&
        (int32_t)(now_ms - s_cfg_success_from_ms) >= (int32_t)MENU_CFG_SUCCESS_HOLD_MS) {
        MenuUi_SetConfigSession(0u);
    }
}
void PanelEspManager_SetRemoteStatus(uint8_t esp_enabled, uint8_t online, uint8_t host_connected, uint8_t session_active)
{
    s_esp_enabled = (esp_enabled != 0u) ? 1u : 0u;
    s_esp_online = (online != 0u) ? 1u : 0u;
    s_esp_host_connected = (host_connected != 0u) ? 1u : 0u;
    s_esp_wifi_session = (session_active != 0u) ? 1u : 0u;
}
void PanelConnectionCache_SetRemoteStatus(uint8_t user_wifi_on, uint8_t rs485_on)
{
    s_user_wifi_on = (user_wifi_on != 0u) ? 1u : 0u;
    PPKYConfig.rs485_on = (rs485_on != 0u) ? 1u : 0u;
}
void MenuUi_SetMcuDetailSlot(uint8_t cfg_slot) { s_mcu_slot = cfg_slot; }
uint8_t MenuUi_GetMcuDetailSlot(void) { return s_mcu_slot; }
void MenuUi_SetBlockZoneSelected(uint8_t zone_idx) { s_block_zone_selected = zone_idx; }
uint8_t MenuUi_GetBlockZoneSelected(void) { return s_block_zone_selected; }
void MenuUi_SetConnectionSelected(uint8_t idx) { s_connection_selected = idx & 0x01u; }
uint8_t MenuUi_GetConnectionSelected(void) { return s_connection_selected; }
void MenuUi_SetWifiBlocked(uint8_t blocked) { s_wifi_blocked = (blocked != 0u) ? 1u : 0u; }
uint8_t MenuUi_IsWifiBlocked(void) { return s_wifi_blocked; }

void MenuUi_SetMenuSelected(uint16_t selected) { s_menu_selected = selected; }
uint16_t MenuUi_GetMenuSelected(void) { return s_menu_selected; }
void MenuUi_SetMenuNItems(uint8_t n_items) { s_menu_n_items = n_items; }
uint8_t MenuUi_GetMenuNItems(void) { return s_menu_n_items; }
void MenuUi_RequestIndicationTest(void) { s_menu_indication_test_req = 1u; }
uint8_t MenuUi_ConsumeIndicationTestRequest(void)
{
    uint8_t pending = s_menu_indication_test_req;
    s_menu_indication_test_req = 0u;
    return pending;
}
void MenuUi_SetFireModeValue(uint8_t mode) { s_menu_fire_mode = mode; }
uint8_t MenuUi_GetFireModeValue(void) { return s_menu_fire_mode; }
void MenuUi_SetSoundValue(uint8_t sound_on, uint8_t blocked)
{
    s_menu_sound_on = (sound_on != 0u) ? 1u : 0u;
    s_menu_sound_blocked = (blocked != 0u) ? 1u : 0u;
}
uint8_t MenuUi_GetSoundValue(void) { return s_menu_sound_on; }
uint8_t MenuUi_IsSoundBlocked(void) { return s_menu_sound_blocked; }

void EspManager_Init(void) {}
void EspManager_Process(uint32_t now_ms) { (void)now_ms; }
void EspManager_OnEspPoweredOn(void) {}
void EspManager_OnEspPoweredOff(void) {}
void EspManager_OnActivity(const uint8_t *payload, uint16_t len) { (void)payload; (void)len; }
void EspManager_RequestWifiEnable(void) {}
uint8_t EspManager_IsOnline(void) { return s_esp_online; }
uint8_t EspManager_IsWifiEnabled(void) { return s_esp_enabled; }
uint8_t EspManager_IsHostConnected(void) { return s_esp_host_connected; }
uint8_t EspManager_IsLinkActive(void) { return (uint8_t)(s_esp_enabled != 0u && s_esp_online != 0u && s_esp_host_connected != 0u); }
uint8_t EspManager_IsWifiSessionActive(void) { return (uint8_t)(s_esp_wifi_session != 0u || s_esp_host_connected != 0u); }
uint8_t EspManager_IsUserWifiOn(void) { return s_user_wifi_on; }
uint8_t EspManager_IsWifiIconVisible(uint32_t now_ms)
{
    if (EspManager_IsWifiSessionActive() == 0u) {
        return 0u;
    }
    if (EspManager_IsLinkActive() != 0u) {
        return 1u;
    }
    /* 1 Гц: 500 мс вкл / 500 мс выкл */
    return (uint8_t)(((now_ms / 500u) & 1u) == 0u);
}

static uint8_t s_pending_zone_mode_ui_change = 0u;
static uint8_t s_pending_zone_mode_ui_change_idx = 0u;

uint8_t PPKY_ZoneFireModeGet(uint8_t zone_idx)
{
	/* zone_idx в UI хранится 0-based. */
	if (zone_idx >= ZONE_NUMBER) {
		return 0u;
	}
	return PPKYConfig.zone_fire_mode[zone_idx];
}

void PPKY_ZoneFireModeSet(uint8_t zone_idx, uint8_t mode)
{
	if (zone_idx >= ZONE_NUMBER) {
		return;
	}
	PPKYConfig.zone_fire_mode[zone_idx] = (uint8_t)(mode & 0x03u);
}

uint8_t PPKY_ZoneEffectiveMode(uint8_t zone_can)
{
	/* zone_can 1-based в CAN, на UI обычно +1. */
	if (zone_can == 0u) {
		return 0u;
	}
	const uint8_t zone_idx = (uint8_t)(zone_can - 1u);
	return PPKY_ZoneFireModeGet(zone_idx);
}

uint8_t PPKY_ZoneLaunchBlockedByCanZone(uint8_t zone_can)
{
	return (PPKY_ZoneEffectiveMode(zone_can) == 3u) ? 1u : 0u;
}

uint8_t PPKY_ZoneIsManualByCanZone(uint8_t zone_can)
{
	return (PPKY_ZoneEffectiveMode(zone_can) == 2u) ? 1u : 0u;
}

uint8_t PPKY_AnyZoneManualOrBlocked(void)
{
	for (uint8_t i = 0u; i < ZONE_NUMBER; i++) {
		const uint8_t m = PPKYConfig.zone_fire_mode[i];
		if (m == 2u || m == 3u) {
			return 1u;
		}
	}
	return 0u;
}
void PPKY_ZoneFireModeInitFromGlobal(void) {}
void PPKY_ZoneModeUiNotify(uint8_t zone_idx)
{
	if (zone_idx >= ZONE_NUMBER) {
		return;
	}
	s_pending_zone_mode_ui_change = 1u;
	s_pending_zone_mode_ui_change_idx = zone_idx;
}

uint8_t PPKY_ZoneModeUiConsumeNew(uint8_t *zone_idx_out)
{
	if (zone_idx_out == 0 || s_pending_zone_mode_ui_change == 0u) {
		return 0u;
	}
	*zone_idx_out = s_pending_zone_mode_ui_change_idx;
	s_pending_zone_mode_ui_change = 0u;
	return 1u;
}

void ConfigIgnBlockSync_Init(void) {}
void ConfigIgnBlockSync_Process1ms(uint32_t now_ms) { (void)now_ms; }
void ConfigIgnBlockSync_Request(void) {}
uint8_t ConfigIgnBlockSync_ShouldDeferCrc(void) { return 0u; }

void EventLog_LogSoundToggle(uint8_t enabled, uint8_t source) { (void)enabled; (void)source; }
void EventLog_LogFireModeChange(uint8_t mode, uint8_t source) { (void)mode; (void)source; }

/* ---------------- ЖУРНАЛ ПО RS (экран «Журнал») ---------------- */

#define PANEL_JOURNAL_MAX_ITEMS 64u

typedef struct {
	uint32_t rec_idx;
	uint32_t ts;
	uint16_t code;
	char short_text[49]; /* max 48 bytes + '\0' */
	char full_text[EVENT_LOG_UI_DETAIL_LEN + 1u];
} PanelJournalEntry_t;

static PanelJournalEntry_t g_journal_entries[PANEL_JOURNAL_MAX_ITEMS];
static uint32_t g_journal_count;

void PanelDeviceCache_SetList(uint8_t selected_slot,
                              uint8_t count,
                              const uint8_t *items,
                              uint16_t items_len,
                              const uint8_t *zone_name,
                              uint8_t zone_name_len)
{
    memset(PPKYConfig.CfgDevices, 0, sizeof(PPKYConfig.CfgDevices));

    uint16_t pos = 0u;
    for (uint8_t i = 0u; i < count; i++) {
        if ((uint16_t)(pos + 4u) > items_len) {
            break;
        }

        uint8_t slot = items[pos++];
        uint8_t zone = items[pos++];
        uint8_t d_type = items[pos++];
        uint8_t h_adr = items[pos++];

        if (slot >= MAX_MCU_IN_BUS) {
            continue;
        }

        PPKYConfig.CfgDevices[slot].UId.devId.zone = zone;
        PPKYConfig.CfgDevices[slot].UId.devId.l_adr = 0u;
        PPKYConfig.CfgDevices[slot].UId.devId.h_adr = h_adr;
        PPKYConfig.CfgDevices[slot].UId.devId.d_type = d_type;
    }

    MenuUi_SetMcuDetailSlot(selected_slot);

    if (selected_slot < MAX_MCU_IN_BUS) {
        uint8_t zone = PPKYConfig.CfgDevices[selected_slot].UId.devId.zone;
        if (zone >= 1u && zone <= ZONE_NUMBER) {
            uint8_t zone_idx = (uint8_t)(zone - 1u);
            memset(PPKYConfig.zone_name[zone_idx], 0, ZONE_NAME_SIZE);
            if (zone_name != 0 && zone_name_len != 0u) {
                if (zone_name_len > ZONE_NAME_SIZE) {
                    zone_name_len = ZONE_NAME_SIZE;
                }
                memcpy(PPKYConfig.zone_name[zone_idx], zone_name, zone_name_len);
            }
        }
    }
}

void PanelDeviceCache_SetDetail(uint8_t slot,
                                uint8_t zone,
                                uint8_t d_type,
                                uint8_t h_adr,
                                uint32_t uid0,
                                uint32_t uid1,
                                uint32_t uid2,
                                const uint8_t *zone_name,
                                uint8_t zone_name_len)
{
    if (slot >= MAX_MCU_IN_BUS) {
        return;
    }

    PPKYConfig.CfgDevices[slot].UId.devId.zone = zone;
    PPKYConfig.CfgDevices[slot].UId.devId.l_adr = 0u;
    PPKYConfig.CfgDevices[slot].UId.devId.h_adr = h_adr;
    PPKYConfig.CfgDevices[slot].UId.devId.d_type = d_type;
    PPKYConfig.CfgDevices[slot].UId.UId0 = uid0;
    PPKYConfig.CfgDevices[slot].UId.UId1 = uid1;
    PPKYConfig.CfgDevices[slot].UId.UId2 = uid2;
    MenuUi_SetMcuDetailSlot(slot);

    if (zone >= 1u && zone <= ZONE_NUMBER) {
        uint8_t zone_idx = (uint8_t)(zone - 1u);
        memset(PPKYConfig.zone_name[zone_idx], 0, ZONE_NAME_SIZE);
        if (zone_name != 0 && zone_name_len != 0u) {
            if (zone_name_len > ZONE_NAME_SIZE) {
                zone_name_len = ZONE_NAME_SIZE;
            }
            memcpy(PPKYConfig.zone_name[zone_idx], zone_name, zone_name_len);
        }
    }
}

void PanelZoneModeCache_SetList(uint8_t selected_zone_idx,
                                uint8_t count,
                                const uint8_t *items,
                                uint16_t items_len)
{
    memset(PPKYConfig.zone_name, 0, sizeof(PPKYConfig.zone_name));
    memset(PPKYConfig.zone_fire_mode, 0, sizeof(PPKYConfig.zone_fire_mode));

    uint16_t pos = 0u;
    for (uint8_t i = 0u; i < count; i++) {
        if ((uint16_t)(pos + 3u) > items_len) {
            break;
        }

        uint8_t zone_idx = items[pos++];
        uint8_t mode = items[pos++];
        uint8_t src_name_len = items[pos++];
        uint8_t name_len = src_name_len;

        if (zone_idx >= ZONE_NUMBER) {
            if ((uint16_t)(pos + name_len) > items_len) {
                break;
            }
            pos = (uint16_t)(pos + name_len);
            continue;
        }
        if ((uint16_t)(pos + name_len) > items_len) {
            break;
        }

        if (name_len > ZONE_NAME_SIZE) {
            name_len = ZONE_NAME_SIZE;
        }
        memcpy(PPKYConfig.zone_name[zone_idx], &items[pos], name_len);
        PPKYConfig.zone_fire_mode[zone_idx] = (uint8_t)(mode & 0x03u);
        pos = (uint16_t)(pos + src_name_len);
    }

    if (selected_zone_idx < ZONE_NUMBER) {
        MenuUi_SetBlockZoneSelected(selected_zone_idx);
        PPKY_ZoneModeUiNotify(selected_zone_idx);
    }
}

static void PanelJournalCache_Reset(void)
{
	memset(g_journal_entries, 0, sizeof(g_journal_entries));
	g_journal_count = 0u;
}

uint32_t PanelJournalCache_GetCapacity(void)
{
	return PANEL_JOURNAL_MAX_ITEMS;
}

uint32_t PanelJournalCache_GetCount(void)
{
	return g_journal_count;
}

static uint16_t panel_rs_get_u16le(const uint8_t *src)
{
	return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8);
}

static uint32_t panel_rs_get_u32le(const uint8_t *src)
{
	return (uint32_t)src[0] |
	       ((uint32_t)src[1] << 8) |
	       ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

/* JOURNAL_LIST:
 * total u32, selected_idx u32, window_first u32, n_items u8, then n_items x:
 *   rec_idx u32, ts u32, code u16, text_len u8 + text[text_len]
 */
void PanelJournalCache_SetList(uint32_t total,
				uint32_t selected_idx,
				uint32_t window_first,
				uint8_t n_items,
				const uint8_t *items,
				uint16_t items_len)
{
	uint16_t pos = 0u;
	uint8_t i;

	(void)total;
	(void)selected_idx;
	(void)window_first;

	PanelJournalCache_Reset();

	if (items == 0 || items_len == 0u || n_items == 0u) {
		return;
	}

	if (n_items > PANEL_JOURNAL_MAX_ITEMS) {
		n_items = (uint8_t)PANEL_JOURNAL_MAX_ITEMS;
	}

	for (i = 0u; i < n_items; i++) {
		PanelJournalEntry_t *e = &g_journal_entries[i];
		uint8_t text_len;

		/* rec_idx u32 + ts u32 + code u16 + text_len u8 */
		if ((uint16_t)(pos + 4u + 4u + 2u + 1u) > items_len) {
			break;
		}

		e->rec_idx = panel_rs_get_u32le(&items[pos]);
		pos += 4u;
		e->ts = panel_rs_get_u32le(&items[pos]);
		pos += 4u;
		e->code = panel_rs_get_u16le(&items[pos]);
		pos += 2u;

		text_len = items[pos++];
		if (text_len > 48u) {
			text_len = 48u;
		}

		if ((uint16_t)(pos + text_len) > items_len) {
			break;
		}

		memcpy(e->short_text, &items[pos], text_len);
		e->short_text[text_len] = '\0';
		pos = (uint16_t)(pos + text_len);

		/* Полное поле пока считаем тем же, т.к. JOURNAL_DETAIL приходит отдельно. */
		(void)strncpy(e->full_text, e->short_text, sizeof(e->full_text) - 1u);
		e->full_text[sizeof(e->full_text) - 1u] = '\0';
	}

	g_journal_count = i;
}

/* JOURNAL_DETAIL:
 * rec_idx u32, ts u32, code u16, text_len u16 + text[text_len], raw_len u8 + raw[raw_len] (ignored)
 */
void PanelJournalCache_SetDetail(uint32_t rec_idx,
				   uint32_t ts,
				   uint16_t code,
				   const uint8_t *text,
				   uint16_t text_len)
{
	uint32_t i;
	PanelJournalEntry_t *found = 0;

	if (text == 0 || text_len == 0u) {
		PanelJournalCache_Reset();
		return;
	}

	if (text_len > EVENT_LOG_UI_DETAIL_LEN) {
		text_len = EVENT_LOG_UI_DETAIL_LEN;
	}

	for (i = 0u; i < g_journal_count; i++) {
		if (g_journal_entries[i].rec_idx == rec_idx) {
			found = &g_journal_entries[i];
			break;
		}
	}

	if (found == 0) {
		PanelJournalCache_Reset();
		found = &g_journal_entries[0];
		g_journal_count = 1u;
		found->rec_idx = rec_idx;
	}

	found->ts = ts;
	found->code = code;

	memcpy(found->full_text, text, text_len);
	found->full_text[text_len] = '\0';

	{
		uint16_t sl = (text_len > 48u) ? 48u : text_len;
		memcpy(found->short_text, text, sl);
		found->short_text[sl] = '\0';
	}
}

static bool PanelJournalCache_ReadEntry(uint32_t logical_index, PanelJournalEntry_t *out)
{
	if (out == 0) {
		return false;
	}
	if (logical_index >= g_journal_count) {
		return false;
	}
	*out = g_journal_entries[logical_index];
	return true;
}

bool PanelJournalCache_ReadRecord(uint32_t logical_index, EventLogRecord_t *out_record)
{
	PanelJournalEntry_t e;
	if (!PanelJournalCache_ReadEntry(logical_index, &e) || out_record == 0) {
		return false;
	}

	memset(out_record, 0, sizeof(*out_record));
	out_record->event_code = e.code;
	/* additional[0] используем как индекс записи для EventLogUi_FormatRecord(). */
	out_record->additional[0] = (uint8_t)logical_index;
	return true;
}

void EventLogUi_FormatEmpty(EventLogUiLines_t *out)
{
	if (out == 0) {
		return;
	}
	memset(out, 0, sizeof(*out));
	(void)strncpy(out->header, "Журнал", sizeof(out->header) - 1u);
	(void)strncpy(out->title, "ПУСТО", sizeof(out->title) - 1u);
	(void)strncpy(out->detail, "Нет данных", sizeof(out->detail) - 1u);
}

void EventLogUi_FormatRecord(const EventLogRecord_t *rec,
			     uint32_t display_index_1based,
			     uint32_t count,
			     EventLogUiLines_t *out)
{
	PanelJournalEntry_t e;
	uint32_t logical_index;
	char header[EVENT_LOG_UI_HEADER_LEN];

	if (out == 0 || rec == 0) {
		return;
	}

	logical_index = (uint32_t)rec->additional[0];
	if (!PanelJournalCache_ReadEntry(logical_index, &e)) {
		EventLogUi_FormatEmpty(out);
		return;
	}

	memset(out, 0, sizeof(*out));
	(void)snprintf(header, sizeof(header), "Журнал %lu/%lu",
		       (unsigned long)display_index_1based,
		       (unsigned long)count);
	(void)strncpy(out->header, header, sizeof(out->header) - 1u);
	(void)strncpy(out->title, e.short_text, sizeof(out->title) - 1u);
	(void)strncpy(out->detail, e.full_text, sizeof(out->detail) - 1u);
}

bool EventLogReader_GetTierInfo(uint8_t tier, EventLogTierInfo_t *info)
{
	(void)tier;
	if (info == 0) {
		return false;
	}
	info->capacity = PanelJournalCache_GetCapacity();
	info->count = PanelJournalCache_GetCount();
	info->write_head = 0u;
	return true;
}

bool EventLogReader_ReadLogical(uint8_t tier,
				uint32_t logical_index,
				EventLogRecStatus_t *status,
				EventLogRecord_t *record)
{
	(void)tier;
	if (status == 0 || record == 0) {
		return false;
	}

	if (PanelJournalCache_ReadRecord(logical_index, record)) {
		*status = EVENT_LOG_REC_VALID;
		return true;
	}

	*status = EVENT_LOG_REC_EMPTY;
	return true;
}

uint32_t EventLogReader_GetCatalogCrc32(void)
{
	return 0u;
}
