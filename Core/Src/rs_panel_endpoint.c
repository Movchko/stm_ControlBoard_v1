#include "rs_panel_endpoint.h"

#include <string.h>

#include "beeper.h"
#include "boot_panel.h"
#include "button.h"
#include "event_log_ui.h"
#include "led.h"
#include "main.h"
#include "menu_ui.h"
#include "panel_ui_bridge.h"
#include "rtc_cache.h"
#include "rs_panel_debug.h"

extern PPKYCfg PPKYConfig;
extern UART_HandleTypeDef huart4;
extern RTC_HandleTypeDef hrtc;

/* Journal cache lives in panel_stubs.c (реализация для UI журнала на панели). */
extern void PanelJournalCache_SetList(uint32_t total,
				       uint32_t selected_idx,
				       uint32_t window_first,
				       uint8_t n_items,
				       const uint8_t *items,
				       uint16_t items_len);
extern void PanelJournalCache_SetDetail(uint32_t rec_idx,
					  uint32_t ts,
					  uint16_t code,
					  const uint8_t *text,
					  uint16_t text_len);
extern void PanelDeviceCache_SetList(uint8_t selected_slot,
                                     uint8_t count,
                                     const uint8_t *items,
                                     uint16_t items_len,
                                     const uint8_t *zone_name,
                                     uint8_t zone_name_len);
extern void PanelDeviceCache_SetDetail(uint8_t slot,
                                       uint8_t zone,
                                       uint8_t d_type,
                                       uint8_t h_adr,
                                       uint32_t uid0,
                                       uint32_t uid1,
                                       uint32_t uid2,
                                       const uint8_t *zone_name,
                                       uint8_t zone_name_len);
extern void PanelZoneModeCache_SetList(uint8_t selected_zone_idx,
                                       uint8_t count,
                                       const uint8_t *items,
                                       uint16_t items_len);

static RsPanelEndpoint g_endpoint;
static uint32_t g_activity_accum_ms = 0u;
static uint32_t g_uptime_sec = 0u;

typedef struct {
    uint8_t active;
    uint8_t mode;
    uint8_t remaining_s;
    uint8_t n_zones;
    char zone_names[16][ZONE_NAME_SIZE + 1];
} DeferredFireUi;

typedef struct {
    uint8_t active;
    uint8_t n_items;
    char titles[PANEL_STATE_MAX_WARN_ITEMS][24];
    char details[PANEL_STATE_MAX_WARN_ITEMS][ZONE_NAME_SIZE + 1];
} DeferredWarnUi;

static DeferredFireUi s_deferred_fire;
static DeferredWarnUi s_deferred_warn;
static volatile uint8_t s_fire_pending;
static volatile uint8_t s_warn_pending;

typedef struct {
    uint16_t screen_id;
    uint8_t action;
} DeferredNavUi;

static DeferredNavUi s_deferred_nav;
static volatile uint8_t s_nav_pending;

static void rs_queue_nav(uint16_t screen_id, uint8_t action)
{
    s_deferred_nav.screen_id = screen_id;
    s_deferred_nav.action = action;
    s_nav_pending = 1u;
}

static void rs_queue_fire_ui(uint8_t active,
                             uint8_t mode,
                             uint8_t remaining_s,
                             uint8_t n_zones,
                             char (*zone_names)[ZONE_NAME_SIZE + 1])
{
    uint8_t i;

    s_deferred_fire.active = active;
    s_deferred_fire.mode = mode;
    s_deferred_fire.remaining_s = remaining_s;
    if (n_zones > 16u) {
        n_zones = 16u;
    }
    s_deferred_fire.n_zones = n_zones;
    memset(s_deferred_fire.zone_names, 0, sizeof(s_deferred_fire.zone_names));
    if (zone_names != 0) {
        for (i = 0u; i < n_zones; i++) {
            memcpy(s_deferred_fire.zone_names[i], zone_names[i], ZONE_NAME_SIZE);
            s_deferred_fire.zone_names[i][ZONE_NAME_SIZE] = '\0';
        }
    }
    s_fire_pending = 1u;
    g_rs_panel_dbg.ui_fire_pending = 1u;
}

static void rs_queue_warn_ui(uint8_t active,
                             uint8_t n_items,
                             char (*titles)[24],
                             char (*details)[ZONE_NAME_SIZE + 1])
{
    uint8_t i;

    if (n_items > PANEL_STATE_MAX_WARN_ITEMS) {
        n_items = PANEL_STATE_MAX_WARN_ITEMS;
    }
    s_deferred_warn.active = active;
    s_deferred_warn.n_items = n_items;
    memset(s_deferred_warn.titles, 0, sizeof(s_deferred_warn.titles));
    memset(s_deferred_warn.details, 0, sizeof(s_deferred_warn.details));
    if (titles != 0 && details != 0) {
        for (i = 0u; i < n_items; i++) {
            memcpy(s_deferred_warn.titles[i], titles[i], sizeof(s_deferred_warn.titles[i]) - 1u);
            memcpy(s_deferred_warn.details[i], details[i], sizeof(s_deferred_warn.details[i]) - 1u);
        }
    }
    s_warn_pending = 1u;
    g_rs_panel_dbg.ui_warn_pending = 1u;
}

void RsPanelEndpoint_ProcessDeferredUi(void)
{
    if (s_nav_pending != 0u) {
        s_nav_pending = 0u;
        PanelUiBridge_GotoScreen(s_deferred_nav.screen_id, s_deferred_nav.action);
    }
    if (s_warn_pending != 0u) {
        s_warn_pending = 0u;
        g_rs_panel_dbg.ui_warn_pending = 0u;
        PanelUiBridge_SetWarningStatus(s_deferred_warn.active,
                                       s_deferred_warn.n_items,
                                       s_deferred_warn.titles,
                                       s_deferred_warn.details);
    }
    if (s_fire_pending != 0u) {
        s_fire_pending = 0u;
        g_rs_panel_dbg.ui_fire_pending = 0u;
        PanelUiBridge_SetFireStatus(s_deferred_fire.active,
                                    s_deferred_fire.mode,
                                    s_deferred_fire.remaining_s,
                                    s_deferred_fire.n_zones,
                                    s_deferred_fire.zone_names);
    }
}

static void rs_bus_send_frame(RsPanelEndpoint *endpoint,
                              uint8_t addr,
                              uint8_t seq,
                              uint8_t flags,
                              uint8_t cmd,
                              const uint8_t *payload,
                              uint16_t payload_len)
{
    HAL_StatusTypeDef st;

    if (endpoint == 0) {
        return;
    }
    st = RsBus_SendFrame(&endpoint->bus, addr, seq, flags, cmd, payload, payload_len);
    RsPanelDebug_OnTxFrame(cmd, (st == HAL_OK) ? 1u : 0u);
}

static uint16_t rs_put_u16le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
    return 2u;
}

static uint16_t rs_get_u16le(const uint8_t *src)
{
    return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8);
}

static uint32_t rs_get_u32le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void rs_frag_reset(RsPanelEndpoint *endpoint)
{
    if (endpoint == 0) {
        return;
    }
    endpoint->frag_active = 0u;
    endpoint->frag_id = 0u;
    endpoint->frag_next_idx = 0u;
    endpoint->frag_total = 0u;
    endpoint->frag_len = 0u;
}

static uint8_t rs_bcd_from_bin(uint8_t value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static uint16_t rs_encode_caps(uint8_t *dst, uint16_t dst_size, const RsPanelCaps *caps)
{
    uint16_t pos = 0u;
    uint8_t i;

    if (dst == 0 || caps == 0 || dst_size < 13u) {
        return 0u;
    }

    pos += rs_put_u16le(&dst[pos], caps->fw_ver);
    pos += rs_put_u16le(&dst[pos], caps->hw_id);
    dst[pos++] = caps->ui_profile;
    dst[pos++] = caps->orientation;
    pos += rs_put_u16le(&dst[pos], caps->disp_w);
    pos += rs_put_u16le(&dst[pos], caps->disp_h);
    dst[pos++] = caps->journal_lines;
    dst[pos++] = caps->btn_count;
    for (i = 0u; i < caps->btn_count; i++) {
        dst[pos++] = caps->btn_list[i];
    }
    dst[pos++] = caps->led_count;
    for (i = 0u; i < caps->led_count; i++) {
        dst[pos++] = caps->led_list[i];
    }
    dst[pos++] = caps->flags;
    dst[pos++] = caps->status;
    return pos;
}

static uint8_t rs_decode_poll_req(const uint8_t *src, uint16_t src_len, RsPanelPollReq *out_req)
{
    if (src == 0 || out_req == 0 || src_len < 2u) {
        return 0u;
    }
    out_req->flags = src[0];
    out_req->ack_seq = src[1];
    return 1u;
}

static uint16_t rs_encode_poll_rsp(uint8_t *dst, uint16_t dst_size, const RsPanelPollRsp *rsp)
{
    uint16_t pos = 0u;
    uint8_t i;

    if (dst == 0 || rsp == 0 || dst_size < 3u) {
        return 0u;
    }
    dst[pos++] = rsp->status;
    dst[pos++] = rsp->evt_count;
    for (i = 0u; i < rsp->evt_count; i++) {
        dst[pos++] = rsp->btn_events[i].type;
        dst[pos++] = rsp->btn_events[i].state;
        dst[pos++] = rsp->btn_events[i].level;
    }
    dst[pos++] = rsp->ui_evt_count;
    for (i = 0u; i < rsp->ui_evt_count; i++) {
        dst[pos++] = rsp->ui_events[i].evt_type;
        pos += rs_put_u16le(&dst[pos], rsp->ui_events[i].p1);
        pos += rs_put_u16le(&dst[pos], rsp->ui_events[i].p2);
    }
    return pos;
}

static uint8_t rs_decode_led_cmd(const uint8_t *src, uint16_t src_len, RsPanelLedCmd *out_cmd)
{
    uint16_t pos = 0u;
    uint8_t i;

    if (src == 0 || out_cmd == 0 || src_len < 1u) {
        return 0u;
    }
    memset(out_cmd, 0, sizeof(*out_cmd));
    out_cmd->count = src[pos++];
    if (out_cmd->count > RS_PANEL_MAX_LED_ITEMS) {
        return 0u;
    }
    for (i = 0u; i < out_cmd->count; i++) {
        if ((uint16_t)(pos + 3u) > src_len) {
            return 0u;
        }
        out_cmd->items[i].type = src[pos++];
        out_cmd->items[i].mode = src[pos++];
        out_cmd->items[i].value = src[pos++];
    }
    return 1u;
}

static uint8_t rs_decode_sound_cmd(const uint8_t *src, uint16_t src_len, RsPanelSoundCmd *out_cmd)
{
    if (src == 0 || out_cmd == 0 || src_len < 2u) {
        return 0u;
    }
    memset(out_cmd, 0, sizeof(*out_cmd));
    out_cmd->profile = src[0];
    out_cmd->mute = src[1];
    if (out_cmd->profile == RS_PANEL_SOUND_CUSTOM) {
        if (src_len < 9u) {
            return 0u;
        }
        out_cmd->on_ms = rs_get_u16le(&src[2]);
        out_cmd->off_ms = rs_get_u16le(&src[4]);
        out_cmd->pulses = src[6];
        out_cmd->repeat_ms = rs_get_u16le(&src[7]);
    }
    return 1u;
}

static uint8_t rs_decode_time_cmd(const uint8_t *src, uint16_t src_len, RsPanelTimeCmd *out_cmd)
{
    if (src == 0 || out_cmd == 0 || src_len < 6u) {
        return 0u;
    }
    out_cmd->hour = src[0];
    out_cmd->min = src[1];
    out_cmd->sec = src[2];
    out_cmd->day = src[3];
    out_cmd->month = src[4];
    out_cmd->year = src[5];
    return 1u;
}

static uint8_t rs_decode_ui_nav_cmd(const uint8_t *src, uint16_t src_len, RsPanelUiNavCmd *out_cmd)
{
    if (src == 0 || out_cmd == 0 || src_len < 5u) {
        return 0u;
    }
    out_cmd->screen_id = rs_get_u16le(&src[0]);
    out_cmd->action = src[2];
    out_cmd->param = rs_get_u16le(&src[3]);
    return 1u;
}

static uint8_t rs_decode_profile_set_cmd(const uint8_t *src, uint16_t src_len, RsPanelProfileSetCmd *out_cmd)
{
    if (src == 0 || out_cmd == 0 || src_len < 1u) {
        return 0u;
    }
    memset(out_cmd, 0, sizeof(*out_cmd));
    out_cmd->sub = src[0];
    switch (out_cmd->sub) {
    case RS_PANEL_PROFILE_SET_ORIENTATION:
    case RS_PANEL_PROFILE_SET_BTN_MASK:
    case RS_PANEL_PROFILE_SET_JOURNAL_LINES:
        if (src_len < 2u) {
            return 0u;
        }
        out_cmd->value.orientation = src[1];
        break;
    case RS_PANEL_PROFILE_SET_LED_MASK:
        if (src_len < 3u) {
            return 0u;
        }
        out_cmd->value.led_enable = rs_get_u16le(&src[1]);
        break;
    case RS_PANEL_PROFILE_SET_FACTORY_RESET:
        break;
    default:
        return 0u;
    }
    return 1u;
}

static uint8_t rs_led_type_to_local(uint8_t type)
{
    switch (type) {
    case RS_PANEL_LED_POWER: return LED_POWER;
    case RS_PANEL_LED_NORM: return LED_NORM;
    case RS_PANEL_LED_START: return LED_START;
    case RS_PANEL_LED_STOP: return LED_STOP;
    case RS_PANEL_LED_ERR: return LED_ERR;
    case RS_PANEL_LED_FIRE: return LED_FIRE;
    case RS_PANEL_LED_AUTO_OFF: return LED_AUTO_OFF;
    case RS_PANEL_LED_BUT_START_ALL: return LED_BUT_START_ALL;
    case RS_PANEL_LED_BUT_STOP: return LED_BUT_STOP;
    case RS_PANEL_LED_BUT_START_SP: return LED_BUT_START_SP;
    case RS_PANEL_LED_BUT_ENTER: return LED_BUT_ENTER_UP;
    case RS_PANEL_LED_BUT_ESC: return LED_BUT_ESC_DW;
    case RS_PANEL_LED_LBL_START_ALL: return LED_STR_START_ALL;
    case RS_PANEL_LED_LBL_STOP: return LED_STR_STOP;
    case RS_PANEL_LED_LBL_START_SP: return LED_STR_START_SP;
    default: return 0xFFu;
    }
}

static void rs_apply_leds(const RsPanelLedCmd *cmd)
{
    uint8_t i;

    if (cmd == 0) {
        return;
    }

    for (i = 0u; i < cmd->count; i++) {
        uint8_t led = rs_led_type_to_local(cmd->items[i].type);
        if (led == 0xFFu) {
            continue;
        }

        switch (cmd->items[i].mode) {
        case RS_PANEL_LED_MODE_OFF:
            Led_Set(led, 0u);
            break;
        case RS_PANEL_LED_MODE_ON:
            Led_Set(led, 1u);
            break;
        case RS_PANEL_LED_MODE_BLINK:
            Led_Set(led, 2u);
            break;
        case RS_PANEL_LED_MODE_BRIGHT:
            Led_Set(led, 1u);
            Led_SetBrightness(led, cmd->items[i].value);
            break;
        default:
            break;
        }
    }
}

static void rs_apply_sound(const RsPanelSoundCmd *cmd)
{
    if (cmd == 0) {
        return;
    }

    PPKYConfig.beep = (cmd->mute == 0u) ? 1u : 0u;
    Beeper_SoundOnOff(cmd->mute == 0u);
    if (cmd->mute != 0u) {
        Beeper_StopPattern();
        Beeper_FireAlarmOff();
        return;
    }

    switch (cmd->profile) {
    case RS_PANEL_SOUND_OFF:
        Beeper_StopPattern();
        Beeper_FireAlarmOff();
        break;
    case RS_PANEL_SOUND_FAULT:
        Beeper_StartPulseTrain(BEEPER_PATTERN_FAULT_ON_MS,
                               BEEPER_PATTERN_FAULT_OFF_MS,
                               BEEPER_PATTERN_FAULT_PULSES,
                               BEEPER_PATTERN_FAULT_REPEAT_MS);
        break;
    case RS_PANEL_SOUND_ATTN:
        Beeper_StartPulseTrain(SOUND_ATTN_DUTY_ON_MS,
                               SOUND_ATTN_DUTY_OFF_MS,
                               SOUND_ATTN_DUTY_PULSES,
                               SOUND_ATTN_DUTY_REPEAT_MS);
        break;
    case RS_PANEL_SOUND_FIRE:
        Beeper_FireAlarmOn();
        break;
    case RS_PANEL_SOUND_FIRE1:
        Beeper_StartPulseTrain(SOUND_FIRE1_DUTY_ON_MS,
                               SOUND_FIRE1_DUTY_OFF_MS,
                               SOUND_FIRE1_DUTY_PULSES,
                               SOUND_FIRE1_DUTY_REPEAT_MS);
        break;
    case RS_PANEL_SOUND_START:
        Beeper_StartPulseTrain(BEEPER_PATTERN_START_ON_MS,
                               BEEPER_PATTERN_START_OFF_MS,
                               BEEPER_PATTERN_START_PULSES,
                               BEEPER_PATTERN_START_REPEAT_MS);
        break;
    case RS_PANEL_SOUND_START_ALL_HOLD:
        Beeper_StartPulseTrain(SOUND_START_ALL_HOLD_DUTY_MS,
                               SOUND_START_ALL_HOLD_PERIOD_MS - SOUND_START_ALL_HOLD_DUTY_MS,
                               1u,
                               SOUND_START_ALL_HOLD_PERIOD_MS);
        break;
    case RS_PANEL_SOUND_BTN_ACK:
        Beeper_ButtonAcknowledge();
        break;
    case RS_PANEL_SOUND_CUSTOM:
        Beeper_StartPulseTrain(cmd->on_ms, cmd->off_ms, cmd->pulses, cmd->repeat_ms);
        break;
    default:
        break;
    }
}

static void rs_apply_datetime(const RsPanelTimeCmd *cmd)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    if (cmd == 0) {
        return;
    }

    time.Hours = rs_bcd_from_bin(cmd->hour);
    time.Minutes = rs_bcd_from_bin(cmd->min);
    time.Seconds = rs_bcd_from_bin(cmd->sec);
    date.Year = rs_bcd_from_bin(cmd->year);
    date.Month = rs_bcd_from_bin(cmd->month);
    date.Date = rs_bcd_from_bin(cmd->day);
    date.WeekDay = RTC_WEEKDAY_MONDAY;

    (void)HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BCD);
    (void)HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BCD);
    RtcCache_Refresh();
}

static void rs_apply_main_fire(PanelStateContext *state, const uint8_t *payload, uint16_t len)
{
    uint16_t pos = 0u;
    uint8_t active;
    uint8_t mode;
    uint8_t remaining_s;
    uint8_t n_zones;
    uint8_t i;
    char zone_names[16][ZONE_NAME_SIZE + 1];

    if (state == 0 || payload == 0 || len < 5u) {
        return;
    }

    memset(zone_names, 0, sizeof(zone_names));
    active = (uint8_t)(payload[pos++] & 0x01u);
    state->fire_active = active;
    mode = payload[pos++];
    remaining_s = payload[pos++];
    pos++; /* sel_index */
    n_zones = payload[pos++];
    if (n_zones > 16u) {
        n_zones = 16u;
    }
    for (i = 0u; i < n_zones; i++) {
        uint8_t str_len;
        if (pos >= len) {
            break;
        }
        str_len = payload[pos++];
        if ((uint16_t)(pos + str_len) > len) {
            break;
        }
        if (str_len > ZONE_NAME_SIZE) {
            str_len = ZONE_NAME_SIZE;
        }
        memcpy(zone_names[i], &payload[pos], str_len);
        zone_names[i][str_len] = '\0';
        pos = (uint16_t)(pos + payload[pos - 1u]);
    }

    rs_queue_fire_ui(active, mode, remaining_s, n_zones, zone_names);
}

static void rs_apply_main_warn(PanelStateContext *state, const uint8_t *payload, uint16_t len)
{
    uint16_t pos = 0u;
    uint8_t count;
    uint8_t n_items;
    uint8_t i;

    if (state == 0 || payload == 0 || len < 5u) {
        return;
    }

    memset(state->warning_titles, 0, sizeof(state->warning_titles));
    memset(state->warning_details, 0, sizeof(state->warning_details));

    count = payload[pos++];
    pos++;      /* ver */
    pos += 2u;  /* crc16 */
    n_items = payload[pos++];
    if (n_items > PANEL_STATE_MAX_WARN_ITEMS) {
        n_items = PANEL_STATE_MAX_WARN_ITEMS;
    }

    for (i = 0u; i < n_items && pos < len; i++) {
        uint8_t title_len;
        uint8_t detail_len;

        pos++; /* flags */
        if (pos >= len) {
            break;
        }
        title_len = payload[pos++];
        if ((uint16_t)(pos + title_len) > len) {
            break;
        }
        if (title_len > 23u) {
            title_len = 23u;
        }
        memcpy(state->warning_titles[i], &payload[pos], title_len);
        state->warning_titles[i][title_len] = '\0';
        pos = (uint16_t)(pos + payload[pos - 1u]);

        if (pos >= len) {
            break;
        }
        detail_len = payload[pos++];
        if ((uint16_t)(pos + detail_len) > len) {
            break;
        }
        if (detail_len > ZONE_NAME_SIZE) {
            detail_len = ZONE_NAME_SIZE;
        }
        memcpy(state->warning_details[i], &payload[pos], detail_len);
        state->warning_details[i][detail_len] = '\0';
        pos = (uint16_t)(pos + payload[pos - 1u]);
    }

    g_rs_panel_dbg.last_warn_active = ((count != 0u) && (i != 0u)) ? 1u : 0u;
    g_rs_panel_dbg.last_warn_n_items = i;
    g_rs_panel_dbg.host_warn_data_rx++;

    rs_queue_warn_ui(g_rs_panel_dbg.last_warn_active,
                     i,
                     state->warning_titles,
                     state->warning_details);
}

static void rs_apply_ui_data(PanelStateContext *state, const uint8_t *payload, uint16_t len)
{
    if (state == 0 || payload == 0 || len == 0u) {
        return;
    }

    g_rs_panel_dbg.last_ui_data_sub_id = payload[0];

    switch (payload[0]) {
    case RS_PANEL_UI_DATA_MAIN_FIRE:
        rs_apply_main_fire(state, &payload[1], (uint16_t)(len - 1u));
        break;
    case RS_PANEL_UI_DATA_MAIN_WARN:
        rs_apply_main_warn(state, &payload[1], (uint16_t)(len - 1u));
        break;
    case RS_PANEL_UI_DATA_DATETIME:
        {
            RsPanelTimeCmd time_cmd;
            if (rs_decode_time_cmd(&payload[1], (uint16_t)(len - 1u), &time_cmd)) {
                rs_apply_datetime(&time_cmd);
            }
        }
        break;
    case RS_PANEL_UI_DATA_CONNECTION_STATUS:
        /* [selected_idx][wifi_blocked][esp_enabled][online][host_connected][session?][wifi_on?][rs485_on?] */
        if (len >= 6u) {
            MenuUi_SetConnectionSelected(payload[1]);
            MenuUi_SetWifiBlocked(payload[2]);
            Esp32_SetEnabled(payload[3]);
            {
                uint8_t session = (len >= 7u) ? payload[6] : ((payload[5] != 0u) ? 1u : 0u);
                PanelEspManager_SetRemoteStatus(payload[3], payload[4], payload[5], session);
            }
            if (len >= 9u) {
                PanelConnectionCache_SetRemoteStatus(payload[7], payload[8]);
            }
        }
        break;
    case RS_PANEL_UI_DATA_CONFIG_STATUS:
        /* [state][percent] */
        if (len >= 3u) {
            MenuConfig_SetRemoteStatus((MenuCfgState)payload[1], payload[2]);
        }
        break;
    case RS_PANEL_UI_DATA_MENU_LIST:
        {
            /* selected u16 + n_items u8 + items[] (len u8 + utf8[len]) */
            if (len < 1u + 2u + 1u) {
                break;
            }
            const uint8_t *p = &payload[1];
            uint16_t selected = rs_get_u16le(&p[0]);
            uint8_t n_items = p[2];

            /* selected can be out of bounds for this panel;
             * clamp later in ScreenMenuPresenter if needed. */
            MenuUi_SetMenuSelected(selected);
            MenuUi_SetMenuNItems(n_items);

            /* Skip items payload to validate frame bounds. */
            uint16_t pos = (uint16_t)(2u + 1u);
            uint16_t max_pos = (uint16_t)(len - 1u);
            for (uint8_t i = 0u; i < n_items; i++) {
                if (pos >= max_pos) {
                    break;
                }
                uint8_t slen = p[pos];
                pos++;
                if ((uint16_t)(pos + slen) > max_pos) {
                    break;
                }
                pos = (uint16_t)(pos + slen);
            }
        }
        break;
    case RS_PANEL_UI_DATA_MENU_VALUE:
        /* [item_id u8][value u8] */
        if (len >= 3u) {
            uint8_t item_id = payload[1];
            uint8_t value = payload[2];
            if (item_id == 0u) {
                MenuUi_SetFireModeValue(value);
            }
        }
        break;
    case RS_PANEL_UI_DATA_MENU_TOGGLE:
        /* [item_id u8][value u8][blocked u8] */
        if (len >= 4u) {
            uint8_t item_id = payload[1];
            uint8_t value = payload[2];
            uint8_t blocked = payload[3];
            if (item_id == 1u) {
                MenuUi_SetSoundValue(value, blocked);
            }
        }
        break;
    case RS_PANEL_UI_DATA_DEVICE_LIST:
        /* [selected_slot u8][count u8][items[count*4]] [zone_name_len u8][zone_name] */
        if (len >= 4u) {
            uint8_t selected_slot = payload[1];
            uint8_t count = payload[2];
            uint16_t items_bytes = (uint16_t)count * 4u;
            if ((uint16_t)(3u + items_bytes + 1u) <= len) {
                uint8_t zone_name_len = payload[3u + items_bytes];
                const uint8_t *zone_name = &payload[3u + items_bytes + 1u];
                if ((uint16_t)(4u + items_bytes + zone_name_len) <= len) {
                    PanelDeviceCache_SetList(selected_slot,
                                             count,
                                             &payload[3],
                                             items_bytes,
                                             zone_name,
                                             zone_name_len);
                }
            }
        }
        break;
    case RS_PANEL_UI_DATA_DEVICE_DETAIL:
        /* [slot][zone][d_type][h_adr][uid0 u32][uid1 u32][uid2 u32][zone_name_len][zone_name] */
        if (len >= 18u) {
            uint8_t slot = payload[1];
            uint8_t zone = payload[2];
            uint8_t d_type = payload[3];
            uint8_t h_adr = payload[4];
            uint32_t uid0 = rs_get_u32le(&payload[5]);
            uint32_t uid1 = rs_get_u32le(&payload[9]);
            uint32_t uid2 = rs_get_u32le(&payload[13]);
            uint8_t zone_name_len = payload[17];
            const uint8_t *zone_name = (len > 18u) ? &payload[18] : 0;
            if ((uint16_t)(18u + zone_name_len) <= len) {
                PanelDeviceCache_SetDetail(slot,
                                           zone,
                                           d_type,
                                           h_adr,
                                           uid0,
                                           uid1,
                                           uid2,
                                           zone_name,
                                           zone_name_len);
            }
        }
        break;
    case RS_PANEL_UI_DATA_ZONE_MODE_LIST:
        /* [selected_zone_idx u8][count u8][items[count*(zone_idx,mode,name_len,name...)]] */
        if (len >= 3u) {
            uint8_t selected_zone_idx = payload[1];
            uint8_t count = payload[2];
            PanelZoneModeCache_SetList(selected_zone_idx,
                                       count,
                                       &payload[3],
                                       (uint16_t)(len - 3u));
        }
        break;
    case RS_PANEL_UI_DATA_MENU_SELF_TEST:
        PanelUiBridge_StartIndicationTest();
        break;
    case RS_PANEL_UI_DATA_JOURNAL_LIST:
        {
            if (len < 1u + 13u) {
                break;
            }
            const uint8_t *p = &payload[1];
            uint16_t pos = 0u;
            uint32_t total = rs_get_u32le(&p[pos]);
            pos += 4u;
            uint32_t selected_idx = rs_get_u32le(&p[pos]);
            pos += 4u;
            uint32_t window_first = rs_get_u32le(&p[pos]);
            pos += 4u;
            uint8_t n_items = p[pos++];
            uint16_t items_len = (uint16_t)(len - 1u - pos);
            if (n_items != 0u && items_len != 0u) {
                PanelJournalCache_SetList(total, selected_idx, window_first, n_items, &p[pos], items_len);
            } else {
                PanelJournalCache_SetList(total, selected_idx, window_first, 0u, 0, 0u);
            }
        }
        break;
    case RS_PANEL_UI_DATA_JOURNAL_DETAIL:
        {
            /* rec_idx u32 + ts u32 + code u16 + text_len u16 */
            if (len < 1u + 4u + 4u + 2u + 2u) {
                break;
            }
            const uint8_t *p = &payload[1];
            uint16_t pos = 0u;
            uint32_t rec_idx = rs_get_u32le(&p[pos]);
            pos += 4u;
            uint32_t ts = rs_get_u32le(&p[pos]);
            pos += 4u;
            uint16_t code = rs_get_u16le(&p[pos]);
            pos += 2u;
            uint16_t text_len = rs_get_u16le(&p[pos]);
            pos += 2u;

            if ((uint16_t)(pos + text_len) > (uint16_t)(len - 1u)) {
                break;
            }

            PanelJournalCache_SetDetail(rec_idx, ts, code, &p[pos], text_len);
        }
        break;
    default:
        break;
    }
}

static void rs_send_caps(RsPanelEndpoint *endpoint, uint8_t addr, uint8_t seq)
{
    uint8_t payload[64];
    uint16_t payload_len = rs_encode_caps(payload, sizeof(payload), &endpoint->state.caps);
    if (payload_len == 0u) {
        return;
    }
    (void)rs_bus_send_frame(endpoint, addr, seq, RS_BUS_FLAG_DIR, RS_PANEL_RSP_CAPS, payload, payload_len);
}

static void rs_send_ack(RsPanelEndpoint *endpoint, uint8_t addr, uint8_t seq, uint8_t ack_seq)
{
    uint8_t payload[1];
    payload[0] = ack_seq;
    rs_bus_send_frame(endpoint, addr, seq, RS_BUS_FLAG_DIR, RS_PANEL_RSP_ACK, payload, sizeof(payload));
}

static void rs_send_poll_rsp(RsPanelEndpoint *endpoint, uint8_t addr, uint8_t seq)
{
    RsPanelPollRsp rsp;
    uint8_t payload[96];
    uint16_t payload_len;

    PanelState_FillPollResponse(&endpoint->state, &rsp);
    payload_len = rs_encode_poll_rsp(payload, sizeof(payload), &rsp);
    if (payload_len == 0u) {
        return;
    }
    RsPanelDebug_OnPollRsp(rsp.ui_evt_count, rsp.evt_count);
    rs_bus_send_frame(endpoint, addr, seq, RS_BUS_FLAG_DIR, RS_PANEL_RSP_POLL, payload, payload_len);
}

static void rs_send_activity(RsPanelEndpoint *endpoint)
{
    uint8_t payload[RS_PANEL_ACTIVITY_PAYLOAD_SIZE];
    uint16_t pos = 0u;

    if (endpoint == 0) {
        return;
    }
    payload[pos++] = (uint8_t)RS_BUS_DEV_TYPE_PANEL_APP;
    pos = (uint16_t)(pos + rs_put_u16le(&payload[pos], endpoint->state.caps.fw_ver));
    pos = (uint16_t)(pos + rs_put_u16le(&payload[pos], endpoint->state.caps.hw_id));
    payload[pos++] = endpoint->state.caps.status;
    payload[pos++] = (uint8_t)(g_uptime_sec & 0xFFu);
    payload[pos++] = (uint8_t)((g_uptime_sec >> 8) & 0xFFu);
    payload[pos++] = (uint8_t)((g_uptime_sec >> 16) & 0xFFu);
    payload[pos++] = (uint8_t)((g_uptime_sec >> 24) & 0xFFu);
    rs_bus_send_frame(endpoint,
                      endpoint->panel_addr,
                      endpoint->next_tx_seq++,
                      RS_BUS_FLAG_DIR,
                      RS_PANEL_RSP_ACTIVITY,
                      payload,
                      pos);
}

static void rs_endpoint_on_frame(const RsBusFrameView *frame, void *ctx)
{
    RsPanelEndpoint *endpoint = (RsPanelEndpoint *)ctx;

    if (endpoint == 0 || frame == 0) {
        return;
    }
    if ((frame->flags & RS_BUS_FLAG_DIR) != 0u) {
        RsPanelDebug_OnHostFrameIgnored();
        return;
    }
    if (frame->addr != RS_BUS_BROADCAST_ADDR && frame->addr != endpoint->panel_addr) {
        RsPanelDebug_OnHostFrameIgnored();
        return;
    }

    RsPanelDebug_OnHostFrame(frame->cmd, frame->seq, frame->addr);
    g_rs_panel_dbg.current_screen = endpoint->state.current_screen;

    endpoint->state.last_host_seq = frame->seq;

    if ((frame->flags & RS_BUS_FLAG_FRAG) != 0u) {
        uint8_t frag_id;
        uint8_t frag_idx;
        uint8_t frag_total;
        uint16_t chunk_len;

        if (frame->payload_len < 3u) {
            rs_frag_reset(endpoint);
            return;
        }

        frag_id = frame->payload[0];
        frag_idx = frame->payload[1];
        frag_total = frame->payload[2];
        chunk_len = (uint16_t)(frame->payload_len - 3u);

        if (frag_total == 0u || frag_idx >= frag_total) {
            rs_frag_reset(endpoint);
            return;
        }

        if (frag_idx == 0u) {
            endpoint->frag_active = 1u;
            endpoint->frag_id = frag_id;
            endpoint->frag_next_idx = 0u;
            endpoint->frag_total = frag_total;
            endpoint->frag_len = 0u;
        }

        if (endpoint->frag_active == 0u ||
            endpoint->frag_id != frag_id ||
            endpoint->frag_total != frag_total ||
            endpoint->frag_next_idx != frag_idx ||
            (uint32_t)endpoint->frag_len + chunk_len > sizeof(endpoint->frag_buf)) {
            rs_frag_reset(endpoint);
            return;
        }

        if (chunk_len != 0u) {
            memcpy(&endpoint->frag_buf[endpoint->frag_len], &frame->payload[3], chunk_len);
            endpoint->frag_len = (uint16_t)(endpoint->frag_len + chunk_len);
        }
        endpoint->frag_next_idx++;

        if (frag_idx + 1u == frag_total) {
            rs_apply_ui_data(&endpoint->state, endpoint->frag_buf, endpoint->frag_len);
            rs_frag_reset(endpoint);
            if ((frame->flags & RS_BUS_FLAG_ACK_REQ) != 0u) {
                rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
            }
        }
        return;
    }

    switch (frame->cmd) {
    case RS_PANEL_CMD_CAPS_REQ:
        rs_send_caps(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++);
        break;
    case RS_PANEL_CMD_POLL:
        {
            RsPanelPollReq req;
            if (rs_decode_poll_req(frame->payload, frame->payload_len, &req)) {
                (void)req;
                rs_send_poll_rsp(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++);
            }
        }
        break;
    case RS_PANEL_CMD_LED:
        {
            RsPanelLedCmd led_cmd;
            if (rs_decode_led_cmd(frame->payload, frame->payload_len, &led_cmd)) {
                rs_apply_leds(&led_cmd);
            }
        }
        if ((frame->flags & RS_BUS_FLAG_ACK_REQ) != 0u) {
            rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        }
        break;
    case RS_PANEL_CMD_SOUND:
        {
            RsPanelSoundCmd sound_cmd;
            if (rs_decode_sound_cmd(frame->payload, frame->payload_len, &sound_cmd)) {
                rs_apply_sound(&sound_cmd);
            }
        }
        if ((frame->flags & RS_BUS_FLAG_ACK_REQ) != 0u) {
            rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        }
        break;
    case RS_PANEL_CMD_UI_NAV:
        {
            RsPanelUiNavCmd nav_cmd;
            if (rs_decode_ui_nav_cmd(frame->payload, frame->payload_len, &nav_cmd)) {
                endpoint->state.current_screen = nav_cmd.screen_id;
                rs_queue_nav(nav_cmd.screen_id, nav_cmd.action);
            }
        }
        if ((frame->flags & RS_BUS_FLAG_ACK_REQ) != 0u) {
            rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        }
        break;
    case RS_PANEL_CMD_UI_DATA:
        rs_apply_ui_data(&endpoint->state, frame->payload, frame->payload_len);
        if ((frame->flags & RS_BUS_FLAG_ACK_REQ) != 0u) {
            rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        }
        break;
    case RS_PANEL_CMD_TIME:
        {
            RsPanelTimeCmd time_cmd;
            if (rs_decode_time_cmd(frame->payload, frame->payload_len, &time_cmd)) {
                rs_apply_datetime(&time_cmd);
            }
        }
        break;
    case RS_PANEL_CMD_PROFILE_SET:
        {
            RsPanelProfileSetCmd profile_cmd;
            if (rs_decode_profile_set_cmd(frame->payload, frame->payload_len, &profile_cmd)) {
                PanelState_ApplyProfileSet(&endpoint->state, &profile_cmd);
                rs_send_caps(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++);
            }
        }
        break;
    case RS_PANEL_CMD_PANEL_RESET:
        PanelState_ResetUi(&endpoint->state);
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_LOGO, RS_PANEL_UI_ACTION_REPLACE);
        rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        break;
    case RS_PANEL_CMD_ENTER_BOOTLOADER:
        /* ACK уходит до reset; ПО/ППКУ прокидывает кадр как есть. */
        rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        PanelBoot_SetUpdateRequest(endpoint->panel_addr);
        HAL_Delay(20);
        NVIC_SystemReset();
        break;
    default:
        break;
    }
}

void RsPanelEndpoint_Init(void)
{
    memset(&g_endpoint, 0, sizeof(g_endpoint));
    RsPanelDebug_Reset();
    g_endpoint.panel_addr = 0x01u;
    PanelBoot_SetRsAddr(g_endpoint.panel_addr);
    g_endpoint.next_tx_seq = 1u;
    PanelState_Init(&g_endpoint.state);
    RsBus_Init(&g_endpoint.bus, &huart4, BRP_485_EN_GPIO_Port, BRP_485_EN_Pin, rs_endpoint_on_frame, &g_endpoint);
}

void RsPanelEndpoint_Timer10ms(void)
{
    /* UI применяем из TouchGFX tick (после перехода logo→MAIN), а не отсюда:
     * иначе WARN попадает в уничтожаемый view, а setupScreen снова рисует «НОРМА». */
    PanelState_SampleButtons(&g_endpoint.state);
    g_rs_panel_dbg.current_screen = g_endpoint.state.current_screen;
    g_rs_panel_dbg.pending_ui_count = g_endpoint.state.pending_ui_count;
    g_rs_panel_dbg.pending_btn_count = g_endpoint.state.pending_btn_count;
    RsPanelDebug_Timer10ms(HAL_GetTick());
    g_activity_accum_ms += 10u;
    if (g_activity_accum_ms >= 1000u) {
        g_activity_accum_ms = 0u;
        g_uptime_sec++;
        rs_send_activity(&g_endpoint);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart != &huart4 || size == 0u) {
        return;
    }
    RsPanelDebug_OnRxDma(size);
    RsBus_ProcessRxBytes(&g_endpoint.bus, g_endpoint.bus.rx_dma_buf, size);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart4,
                                       g_endpoint.bus.rx_dma_buf,
                                       sizeof(g_endpoint.bus.rx_dma_buf));
    if (huart4.hdmarx != 0) {
        __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
    }
}
