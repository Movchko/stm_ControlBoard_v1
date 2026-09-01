#include "rs_panel_debug.h"

#include "main.h"
#include "menu_ui.h"
#include "rs_panel_protocol.h"

extern UART_HandleTypeDef huart4;

void RsPanelDebug_OnRxRearm(void)
{
    g_rs_panel_dbg.rx_rearm_count++;
}

volatile RsPanelRsDebug g_rs_panel_dbg;

void RsPanelDebug_Reset(void)
{
    uint32_t i;
    volatile uint8_t *p = (volatile uint8_t *)&g_rs_panel_dbg;
    for (i = 0u; i < sizeof(g_rs_panel_dbg); i++) {
        p[i] = 0u;
    }
}

void RsPanelDebug_Timer10ms(uint32_t now_ms)
{
    if (g_rs_panel_dbg.last_host_rx_ms != 0u &&
        (now_ms - g_rs_panel_dbg.last_host_rx_ms) < RS_PANEL_DBG_LINK_TIMEOUT_MS) {
        g_rs_panel_dbg.link_ok = 1u;
    } else {
        g_rs_panel_dbg.link_ok = 0u;
    }

    g_rs_panel_dbg.main_screen_active = MenuUi_IsMainScreenActive();
    g_rs_panel_dbg.uart_rx_state = (uint8_t)huart4.RxState;
    g_rs_panel_dbg.rx_arm_ok = (huart4.RxState == HAL_UART_STATE_BUSY_RX) ? 1u : 0u;
    g_rs_panel_dbg.de_pin_level =
        (HAL_GPIO_ReadPin(BRP_485_EN_GPIO_Port, BRP_485_EN_Pin) == GPIO_PIN_SET) ? 1u : 0u;
}

void RsPanelDebug_OnRxDma(uint16_t nbytes)
{
    g_rs_panel_dbg.rx_dma_events++;
    g_rs_panel_dbg.rx_raw_bytes += nbytes;
}

void RsPanelDebug_OnHostFrame(uint8_t cmd, uint8_t seq, uint8_t addr)
{
    g_rs_panel_dbg.rx_frames_ok++;
    g_rs_panel_dbg.last_host_cmd = cmd;
    g_rs_panel_dbg.last_host_seq = seq;
    g_rs_panel_dbg.last_host_addr = addr;
    g_rs_panel_dbg.last_host_rx_ms = HAL_GetTick();

    switch (cmd) {
    case RS_PANEL_CMD_CAPS_REQ:
        g_rs_panel_dbg.host_caps_rx++;
        g_rs_panel_dbg.caps_ok = 1u;
        break;
    case RS_PANEL_CMD_POLL:
        g_rs_panel_dbg.host_poll_rx++;
        g_rs_panel_dbg.poll_ok = 1u;
        break;
    case RS_PANEL_CMD_UI_NAV:
        g_rs_panel_dbg.host_ui_nav_rx++;
        break;
    case RS_PANEL_CMD_UI_DATA:
        g_rs_panel_dbg.host_ui_data_rx++;
        break;
    case RS_PANEL_CMD_LED:
        g_rs_panel_dbg.host_led_rx++;
        break;
    case RS_PANEL_CMD_SOUND:
        g_rs_panel_dbg.host_sound_rx++;
        break;
    default:
        g_rs_panel_dbg.host_other_rx++;
        break;
    }
}

void RsPanelDebug_OnHostFrameIgnored(void)
{
    g_rs_panel_dbg.rx_frames_ignored++;
}

void RsPanelDebug_OnTxFrame(uint8_t cmd, uint8_t ok)
{
    if (ok != 0u) {
        g_rs_panel_dbg.tx_frames++;
        switch (cmd) {
        case RS_PANEL_RSP_POLL:
            g_rs_panel_dbg.poll_rsp_tx++;
            break;
        case RS_PANEL_RSP_CAPS:
            g_rs_panel_dbg.caps_rsp_tx++;
            break;
        case RS_PANEL_RSP_ACTIVITY:
            g_rs_panel_dbg.activity_tx++;
            break;
        default:
            break;
        }
    } else {
        g_rs_panel_dbg.tx_fail++;
    }
}

void RsPanelDebug_OnPollRsp(uint8_t ui_evt_count, uint8_t btn_evt_count)
{
    g_rs_panel_dbg.ui_evt_tx += ui_evt_count;
    g_rs_panel_dbg.pending_ui_count = ui_evt_count;
    g_rs_panel_dbg.pending_btn_count = btn_evt_count;
}

void RsPanelDebug_OnUiEventQueued(uint8_t evt_type, uint16_t p1)
{
    g_rs_panel_dbg.last_ui_evt_type = evt_type;
    g_rs_panel_dbg.last_ui_evt_p1 = p1;
}
