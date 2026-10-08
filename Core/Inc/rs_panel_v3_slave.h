#ifndef RS_PANEL_V3_SLAVE_H
#define RS_PANEL_V3_SLAVE_H

#include <stdint.h>
#include "rs_panel_protocol_v3.h"
#include "panel_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void RsPanelV3Slave_Init(void);
void RsPanelV3Slave_Timer10ms(void);

/* Применить POLL v3; вернуть 1 если payload v3. */
uint8_t RsPanelV3Slave_OnPoll(const uint8_t *payload, uint16_t len);

/* Собрать RSP v3. */
uint16_t RsPanelV3Slave_BuildRsp(uint8_t *dst, uint16_t dst_size);

uint8_t RsPanelV3Slave_IsSysReady(void);
uint8_t RsPanelV3Slave_HasFaults(void);
uint8_t RsPanelV3Slave_IsFireActive(void);
uint8_t RsPanelV3Slave_IsConfigActive(void);
uint8_t RsPanelV3Slave_IsHoldActive(void);
/** Секунды до конца удержания ПУСК ОБЩИЙ (1..3), 0 если не удерживается. */
uint8_t RsPanelV3Slave_GetHoldRemainingSec(void);

/* Поставить событие в слот (0 = занято/отказ). */
uint8_t RsPanelV3Slave_PostEvent(uint8_t type, uint8_t zone, uint8_t u8_a,
                                 uint16_t u16_a, uint16_t u16_b);

/* Локальный hold ПУСК ОБЩИЙ / ОСТАНОВ (фаза 2). */
void RsPanelV3Slave_OnButtonSample(void);

void RsPanelV3Slave_DrainPanelState(PanelStateContext *ctx);
uint8_t RsPanelV3Slave_IsV3Active(void);

#ifdef __cplusplus
}
#endif

#endif /* RS_PANEL_V3_SLAVE_H */
