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

/* Только ACK полос — безопасно из UART IRQ (до deferred body). */
uint8_t RsPanelV3Slave_OnPollAcks(const uint8_t *payload, uint16_t len);
/* ZONE_NAMES/DEVICES/FAULT_EVT — из UART IRQ (без RsPanelV3Poll на стеке).
 * Иначе один deferred-буфер затирает CLEAR/ADD → devices_crc залипает в 0xFFFF. */
uint8_t RsPanelV3Slave_OnPollStream(const uint8_t *payload, uint16_t len);
/* SYS/ZONES/TIME/… без ACK и без каталог-stream — из main/TouchGFX tick. */
uint8_t RsPanelV3Slave_OnPollBody(const uint8_t *payload, uint16_t len);

/* Собрать RSP v3. */
uint16_t RsPanelV3Slave_BuildRsp(uint8_t *dst, uint16_t dst_size);

uint8_t RsPanelV3Slave_IsSysReady(void);
uint8_t RsPanelV3Slave_HasFaults(void);
uint8_t RsPanelV3Slave_IsFireActive(void);
/** Выбранная CAN-зона пожара (browse); 0 = все / вне выбора. */
uint8_t RsPanelV3Slave_GetSelectedZone(void);
uint8_t RsPanelV3Slave_IsConfigActive(void);
uint8_t RsPanelV3Slave_IsHoldActive(void);
/** Секунды UI-таймера: ПУСК ОБЩИЙ или countdown ПУСК СП (малая), 0 если нет. */
uint8_t RsPanelV3Slave_GetHoldRemainingSec(void);

/* Две независимые EVT-линии (без вытеснения между ними):
 * ordinary — журнал/меню/wifi/…; critical — START_SP/STOP/START_ALL/FIRE_RESET.
 * 0 только при type=NONE. */
uint8_t RsPanelV3Slave_PostEvent(uint8_t type, uint8_t zone, uint8_t u8_a,
                                 uint16_t u16_a, uint16_t u16_b);
/** 1 = ordinary или critical слот занят (ждём ACK). */
uint8_t RsPanelV3Slave_IsEventPending(void);
/** 1 = обе линии пусты (слоты и очереди). */
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
