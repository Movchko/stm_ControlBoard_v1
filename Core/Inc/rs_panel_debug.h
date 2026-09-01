#ifndef RS_PANEL_DEBUG_H
#define RS_PANEL_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Отладка RS485 панель <-> ППКУ (Live Watch / Expressions в отладчике).
 *
 * Главные поля:
 *   g_rs_panel_dbg.link_ok      — 1: от хоста был валидный кадр за последние ~500 мс
 *   g_rs_panel_dbg.caps_ok      — 1: хотя бы раз пришёл CAPS_REQ (0x00)
 *   g_rs_panel_dbg.poll_ok      — 1: хотя бы раз пришёл POLL (0x01)
 *   g_rs_panel_dbg.rx_dma_events — растёт при каждом UART RX (DMA idle); 0 = нет физики/драйвера
 *   g_rs_panel_dbg.rx_frames_ok — принятые кадры master->panel (DIR=0, наш addr)
 *   g_rs_panel_dbg.poll_rsp_tx  — сколько раз панель ответила RSP_POLL
 *   g_rs_panel_dbg.ui_evt_tx    — сколько UI-событий ушло в RSP_POLL (сумма)
 *   g_rs_panel_dbg.last_host_cmd — последняя команда от ППКУ (см. RS_PANEL_CMD_*)
 */
#define RS_PANEL_DBG_LINK_TIMEOUT_MS 500u

typedef struct {
    volatile uint32_t rx_dma_events;
    volatile uint32_t rx_raw_bytes;
    volatile uint32_t rx_frames_ok;
    volatile uint32_t rx_frames_ignored;
    volatile uint32_t tx_frames;
    volatile uint32_t tx_fail;

    volatile uint32_t host_caps_rx;
    volatile uint32_t host_poll_rx;
    volatile uint32_t host_ui_nav_rx;
    volatile uint32_t host_ui_data_rx;
    volatile uint32_t host_warn_data_rx;
    volatile uint32_t host_led_rx;
    volatile uint32_t host_sound_rx;
    volatile uint32_t host_other_rx;

    volatile uint32_t poll_rsp_tx;
    volatile uint32_t caps_rsp_tx;
    volatile uint32_t activity_tx;
    volatile uint32_t ui_evt_tx;

    volatile uint32_t last_ui_evt_type;
    volatile uint32_t last_ui_evt_p1;
    volatile uint8_t  last_host_cmd;
    volatile uint8_t  last_host_seq;
    volatile uint8_t  last_host_addr;
    volatile uint8_t  last_ui_data_sub_id;
    volatile uint8_t  last_warn_active;
    volatile uint8_t  last_warn_n_items;
    volatile uint8_t  ui_warn_pending;
    volatile uint8_t  ui_fire_pending;

    volatile uint8_t  link_ok;
    volatile uint8_t  caps_ok;
    volatile uint8_t  poll_ok;

    volatile uint32_t last_host_rx_ms;
    volatile uint16_t current_screen;
    volatile uint8_t  main_screen_active;
    volatile uint8_t  pending_ui_count;
    volatile uint8_t  pending_btn_count;
    volatile uint8_t  uart_rx_state;
    volatile uint8_t  rx_arm_ok;
    volatile uint8_t  de_pin_level;
    volatile uint32_t rx_rearm_count;
} RsPanelRsDebug;

extern volatile RsPanelRsDebug g_rs_panel_dbg;

void RsPanelDebug_Reset(void);
void RsPanelDebug_Timer10ms(uint32_t now_ms);
void RsPanelDebug_OnRxDma(uint16_t nbytes);
void RsPanelDebug_OnHostFrame(uint8_t cmd, uint8_t seq, uint8_t addr);
void RsPanelDebug_OnHostFrameIgnored(void);
void RsPanelDebug_OnTxFrame(uint8_t cmd, uint8_t ok);
void RsPanelDebug_OnPollRsp(uint8_t ui_evt_count, uint8_t btn_evt_count);
void RsPanelDebug_OnUiEventQueued(uint8_t evt_type, uint16_t p1);

void RsPanelDebug_OnRxRearm(void);

#ifdef __cplusplus
}
#endif

#endif /* RS_PANEL_DEBUG_H */
