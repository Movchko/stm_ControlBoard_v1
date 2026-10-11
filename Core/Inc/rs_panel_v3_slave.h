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

/* В общую очередь EVT (или сразу в слот). Fire-ctrl — после последнего fire
 * в очереди (2-е/2+ место). Обычные — в хвост. 0 только при type=NONE. */
uint8_t RsPanelV3Slave_PostEvent(uint8_t type, uint8_t zone, uint8_t u8_a,
                                 uint16_t u16_a, uint16_t u16_b);
/** 1 = слот события занят (ждём ACK). Очередь может быть непустой отдельно. */
uint8_t RsPanelV3Slave_IsEventPending(void);
/** 1 = слот и очередь пусты (можно ставить фоновый prefetch журнала). */
uint8_t RsPanelV3Slave_IsEventIdle(void);

/* Локальный hold ПУСК ОБЩИЙ / ОСТАНОВ (фаза 2). */
void RsPanelV3Slave_OnButtonSample(void);

void RsPanelV3Slave_DrainPanelState(PanelStateContext *ctx);
uint8_t RsPanelV3Slave_IsV3Active(void);
/** После восстановления связи с ППКУ — снова отдать текущий список неисправностей. */
void RsPanelV3Slave_RequestFaultUiRefresh(void);
/** После unmute — вернуть дежурный звук неисправности, если неисправности ещё есть. */
void RsPanelV3Slave_OnSoundEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* RS_PANEL_V3_SLAVE_H */
