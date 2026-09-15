#ifndef RS_PANEL_ENDPOINT_H
#define RS_PANEL_ENDPOINT_H

#include "rs_bus.h"
#include "panel_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RsBusContext bus;
    PanelStateContext state;
    uint8_t panel_addr;
    uint8_t next_tx_seq;
    uint8_t frag_active;
    uint8_t frag_id;
    uint8_t frag_next_idx;
    uint8_t frag_total;
    uint16_t frag_len;
    uint8_t frag_buf[2048];
    /* Отложенный RSP_DISCOVER (разнос по UID). */
    uint8_t discover_pending;
    uint16_t discover_delay_ticks; /* 10 мс тики */
} RsPanelEndpoint;

void RsPanelEndpoint_Init(void);
void RsPanelEndpoint_Timer10ms(void);
void RsPanelEndpoint_ProcessDeferredUi(void);

#ifdef __cplusplus
}
#endif

#endif /* RS_PANEL_ENDPOINT_H */
