#ifndef PANEL_STATE_H
#define PANEL_STATE_H

#include "button.h"
#include "device_config.h"
#include "rs_panel_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_STATE_MAX_WARN_ITEMS 4u

typedef struct {
    RsPanelCaps caps;
    uint16_t current_screen;
    uint8_t  fire_active;
    uint8_t last_host_seq;
    uint8_t btn_prev_state[NUM_BUTTON];
    uint8_t btn_prev_level[NUM_BUTTON];
    RsPanelButtonEvent pending_btn[RS_PANEL_MAX_POLL_BTN_EVENTS];
    uint8_t pending_btn_count;
    RsPanelUiEvent pending_ui[RS_PANEL_MAX_POLL_UI_EVENTS];
    uint8_t pending_ui_count;
    char warning_titles[PANEL_STATE_MAX_WARN_ITEMS][24];
    char warning_details[PANEL_STATE_MAX_WARN_ITEMS][ZONE_NAME_SIZE + 1];
} PanelStateContext;

void PanelState_Init(PanelStateContext *ctx);
void PanelState_ResetUi(PanelStateContext *ctx);
void PanelState_ApplyProfileSet(PanelStateContext *ctx, const RsPanelProfileSetCmd *cmd);
void PanelState_SampleButtons(PanelStateContext *ctx);
void PanelState_FillPollResponse(PanelStateContext *ctx, RsPanelPollRsp *rsp);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_STATE_H */
