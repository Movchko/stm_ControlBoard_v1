#ifndef INC_MENU_UI_H_
#define INC_MENU_UI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	MENU_CFG_STATE_IDLE = 0,
	MENU_CFG_STATE_RECEIVING,
	MENU_CFG_STATE_APPLYING,
	MENU_CFG_STATE_SUCCESS
} MenuCfgState;

void MenuUi_SetConfigSession(uint8_t active);
uint8_t MenuUi_IsConfigSessionActive(void);

void MenuUi_SetMainScreenActive(uint8_t active);
uint8_t MenuUi_IsMainScreenActive(void);

void Esp32_SetEnabled(uint8_t enabled);
uint8_t Esp32_IsEnabled(void);

void MenuConfig_Reset(void);
void MenuConfig_OnWordReceived(uint16_t word_num);
void MenuConfig_OnSaveCompleted(void);
void MenuConfig_OnApplySuccess(void);

MenuCfgState MenuConfig_GetState(void);
uint8_t MenuConfig_GetPercent(void);

void MenuUi_SetMcuDetailSlot(uint8_t cfg_slot);
uint8_t MenuUi_GetMcuDetailSlot(void);
void MenuUi_SetBlockZoneSelected(uint8_t zone_idx);
uint8_t MenuUi_GetBlockZoneSelected(void);
void MenuUi_SetConnectionSelected(uint8_t idx);
uint8_t MenuUi_GetConnectionSelected(void);
void MenuUi_SetWifiBlocked(uint8_t blocked);
uint8_t MenuUi_IsWifiBlocked(void);
void MenuConfig_SetRemoteStatus(MenuCfgState state, uint8_t percent);
void PanelEspManager_SetRemoteStatus(uint8_t esp_enabled, uint8_t online, uint8_t host_connected, uint8_t session_active);

/* Remote-controlled menu selection.
 * Panel renders highlight based on MENU_LIST{selected,n_items}.
 */
void MenuUi_SetMenuSelected(uint16_t selected);
uint16_t MenuUi_GetMenuSelected(void);
void MenuUi_SetMenuNItems(uint8_t n_items);
uint8_t MenuUi_GetMenuNItems(void);
void MenuUi_RequestIndicationTest(void);
uint8_t MenuUi_ConsumeIndicationTestRequest(void);
void MenuUi_SetFireModeValue(uint8_t mode);
uint8_t MenuUi_GetFireModeValue(void);
void MenuUi_SetSoundValue(uint8_t sound_on, uint8_t blocked);
uint8_t MenuUi_GetSoundValue(void);
uint8_t MenuUi_IsSoundBlocked(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_MENU_UI_H_ */
