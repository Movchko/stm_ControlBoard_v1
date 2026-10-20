#ifndef INC_CONFIG_ZONE_BLOCK_H_
#define INC_CONFIG_ZONE_BLOCK_H_

#include <stdint.h>
#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* zone_fire_mode: 0 авто, 1 автономный, 2 ручной, 3 пуск блок, 4 пожар выкл */
uint8_t PPKY_ZoneFireModeGet(uint8_t zone_idx);
void PPKY_ZoneFireModeSet(uint8_t zone_idx, uint8_t mode);

uint8_t PPKY_ZoneEffectiveMode(uint8_t zone_can);
uint8_t PPKY_ZoneLaunchBlockedByCanZone(uint8_t zone_can);
uint8_t PPKY_ZoneFireBlockedByCanZone(uint8_t zone_can);
uint8_t PPKY_ZoneIsManualByCanZone(uint8_t zone_can);
uint8_t PPKY_AnyZoneManualOrBlocked(void);

void PPKY_ZoneFireModeInitFromGlobal(void);
void PPKY_ZoneModeUiNotify(uint8_t zone_idx);
uint8_t PPKY_ZoneModeUiConsumeNew(uint8_t *zone_idx_out);

#ifdef __cplusplus
}
#endif

#endif /* INC_CONFIG_ZONE_BLOCK_H_ */
