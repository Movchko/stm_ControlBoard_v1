#ifndef PANEL_UI_BRIDGE_H
#define PANEL_UI_BRIDGE_H

#include <stdint.h>
#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void PanelUiBridge_GotoScreen(uint16_t screen_id, uint8_t action);
void PanelUiBridge_SetFireStatus(uint8_t active,
                                 uint8_t mode,
                                 uint8_t remaining_s,
                                 uint8_t n_zones,
                                 char (*zone_names)[ZONE_NAME_SIZE + 1]);
void PanelUiBridge_SetWarningStatus(uint8_t active,
                                    uint8_t n_items,
                                    char (*titles)[24],
                                    char (*details)[ZONE_NAME_SIZE + 1]);
void PanelUiBridge_StartIndicationTest(void);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_UI_BRIDGE_H */
