#include "rs_panel_endpoint.h"
#include "rs_panel_v3_slave.h"
#include "rs_bus_frame.h"

#include <string.h>

#include "beeper.h"
#include "boot_panel.h"
#include "button.h"
#include "event_log_ui.h"
#include "led.h"
#include "main.h"
#include "menu_ui.h"
#include "panel_cfg.h"
#include "panel_host_cache.h"
#include "panel_journal_cache.h"
#include "panel_ui_bridge.h"
#include "rtc_cache.h"
#include "rs_panel_debug.h"
#include "panel_link_monitor.h"
#include "Display/display.h"
#include "backend.h"
#include "upd.h"

extern UART_HandleTypeDef huart4;
extern RTC_HandleTypeDef hrtc;

/* Device/zone caches — в panel_stubs.c. */
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
    uint8_t zone_modes[16];
    uint8_t zone_remaining[16];
} DeferredFireUi;

/* До 16 — как Model / v3 FAULT push (старый v2 лимит PANEL_STATE_MAX_WARN_ITEMS=4). */
#define RS_DEFERRED_WARN_MAX 16u

typedef struct {
    uint8_t active;
    uint8_t n_items;
    char titles[RS_DEFERRED_WARN_MAX][24];
    char details[RS_DEFERRED_WARN_MAX][ZONE_NAME_SIZE + 1];
} DeferredWarnUi;

static DeferredFireUi s_deferred_fire;
static DeferredWarnUi s_deferred_warn;
static volatile uint8_t s_fire_pending;
static volatile uint8_t s_warn_pending;
static volatile uint8_t s_sys_ready_pending;
/* POLL body вне UART IRQ: при FIRE+ZONES полный OnPoll в IDLE держал шину,
 * RSP с event_crit не доходил до Fire_OnPanelStartSp. ACK — сразу в IRQ.
 * Один слот затирал DEV_CLEAR→DEV_ADD: catalog count=0, devices_crc=0xFFFF,
 * а UI неисправностей МКУ жил из FAULT_EVT.mcu — казалось, что «МКУ есть». */
#define RS_DEFER_POLL_Q 8u
static uint8_t s_deferred_poll_q[RS_DEFER_POLL_Q][RS_BUS_MAX_WIRE_PAYLOAD];
static uint16_t s_deferred_poll_q_len[RS_DEFER_POLL_Q];
static volatile uint8_t s_defer_poll_head; /* pop */
static volatile uint8_t s_defer_poll_tail; /* push */
static volatile uint8_t s_defer_poll_n;
/* RSP_POLL не из UART IRQ: @250 кбод TX~2…11 мс в IRQ глотает SysTick
 * (один pending на весь интервал) → «ПУСК ОБЩИЙ» идёт медленно. */
static uint8_t s_pending_poll_rsp[96];
static volatile uint16_t s_pending_poll_rsp_len;
static volatile uint8_t s_pending_poll_rsp_seq;
static volatile uint8_t s_pending_poll_rsp_valid;
typedef struct {
    uint16_t screen_id;
    uint8_t action;
} DeferredNavUi;

static DeferredNavUi s_deferred_nav;
static volatile uint8_t s_nav_pending;

static void rs_poll_rsp_flush(RsPanelEndpoint *endpoint);

static void rs_queue_nav(uint16_t screen_id, uint8_t action)
{
    /* Уход с главного: сразу main=0 и menu-session, чтобы UP/DOWN/ESC шли как
     * UI-события меню ещё до ProcessDeferredUi()/activate Presenter.
     * На MAIN флаг main НЕ трогаем заранее — иначе GotoScreen(MAIN) пропустит переход.
     * Session сбрасываем сразу: иначе ESC/UP после UI_NAV MAIN ещё уйдут как меню. */
    if (screen_id != RS_PANEL_SCREEN_MAIN && screen_id != RS_PANEL_SCREEN_LOGO) {
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetMenuSessionScreen(screen_id);
    } else {
        MenuUi_SetMenuSessionScreen(0u);
    }

    s_deferred_nav.screen_id = screen_id;
    s_deferred_nav.action = action;
    s_nav_pending = 1u;
}

static void rs_queue_fire_ui(uint8_t active,
                             uint8_t mode,
                             uint8_t remaining_s,
                             uint8_t n_zones,
                             char (*zone_names)[ZONE_NAME_SIZE + 1],
                             const uint8_t *zone_modes,
                             const uint8_t *zone_remaining)
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
    memset(s_deferred_fire.zone_modes, 0, sizeof(s_deferred_fire.zone_modes));
    memset(s_deferred_fire.zone_remaining, 0, sizeof(s_deferred_fire.zone_remaining));
    if (zone_names != 0) {
        for (i = 0u; i < n_zones; i++) {
            memcpy(s_deferred_fire.zone_names[i], zone_names[i], ZONE_NAME_SIZE);
            s_deferred_fire.zone_names[i][ZONE_NAME_SIZE] = '\0';
            s_deferred_fire.zone_modes[i] = (zone_modes != 0) ? zone_modes[i] : mode;
            s_deferred_fire.zone_remaining[i] = (zone_remaining != 0) ? zone_remaining[i] : remaining_s;
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

    if (n_items > RS_DEFERRED_WARN_MAX) {
        n_items = (uint8_t)RS_DEFERRED_WARN_MAX;
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

void RsPanelEndpoint_QueueWarningUi(uint8_t active,
                                    uint8_t n_items,
                                    char (*titles)[24],
                                    char (*details)[ZONE_NAME_SIZE + 1])
{
    rs_queue_warn_ui(active, n_items, titles, details);
}

void RsPanelEndpoint_QueueFireUi(uint8_t active,
                                 uint8_t mode,
                                 uint8_t remaining_s,
                                 uint8_t n_zones,
                                 char (*zone_names)[ZONE_NAME_SIZE + 1],
                                 const uint8_t *zone_modes,
                                 const uint8_t *zone_remaining)
{
    rs_queue_fire_ui(active, mode, remaining_s, n_zones, zone_names, zone_modes,
                     zone_remaining);
}

void RsPanelEndpoint_QueueSysReadyNotify(void)
{
    s_sys_ready_pending = 1u;
}

void RsPanelEndpoint_QueueGotoMain(void)
{
    rs_queue_nav(RS_PANEL_SCREEN_MAIN, RS_PANEL_UI_ACTION_REPLACE);
}

static void rs_poll_body_enqueue(const uint8_t *payload, uint16_t len)
{
    uint32_t primask;
    if (payload == 0 || len == 0u || len > RS_BUS_MAX_WIRE_PAYLOAD) {
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_defer_poll_n >= RS_DEFER_POLL_Q) {
        /* Переполнение: вытолкнуть самый старый (лучше потерять ранний, чем
         * затереть единственный слот новым и потерять CLEAR перед ADD). */
        s_defer_poll_head = (uint8_t)((s_defer_poll_head + 1u) % RS_DEFER_POLL_Q);
        s_defer_poll_n--;
    }
    memcpy(s_deferred_poll_q[s_defer_poll_tail], payload, len);
    s_deferred_poll_q_len[s_defer_poll_tail] = len;
    s_defer_poll_tail = (uint8_t)((s_defer_poll_tail + 1u) % RS_DEFER_POLL_Q);
    s_defer_poll_n++;
    if (primask == 0u) {
        __enable_irq();
    }
}

static void rs_poll_body_drain(void)
{
    uint8_t buf[RS_BUS_MAX_WIRE_PAYLOAD];
    uint32_t primask;
    uint8_t guard;

    /* Слить всю очередь за тик — каталог DEV_CLEAR+N×ADD не должен копиться. */
    for (guard = 0u; guard < RS_DEFER_POLL_Q; guard++) {
        uint16_t plen;
        uint8_t head;
        primask = __get_PRIMASK();
        __disable_irq();
        if (s_defer_poll_n == 0u) {
            if (primask == 0u) {
                __enable_irq();
            }
            break;
        }
        head = s_defer_poll_head;
        plen = s_deferred_poll_q_len[head];
        if (plen > (uint16_t)sizeof(buf)) {
            plen = (uint16_t)sizeof(buf);
        }
        if (plen > 0u) {
            memcpy(buf, s_deferred_poll_q[head], plen);
        }
        s_defer_poll_head = (uint8_t)((head + 1u) % RS_DEFER_POLL_Q);
        s_defer_poll_n--;
        if (primask == 0u) {
            __enable_irq();
        }
        if (plen > 0u) {
            (void)RsPanelV3Slave_OnPollBody(buf, plen);
        }
    }
}

void RsPanelEndpoint_FlushPendingTx(void)
{
    rs_poll_rsp_flush(&g_endpoint);
}

void RsPanelEndpoint_ProcessDeferredUi(void)
{
    rs_poll_rsp_flush(&g_endpoint);
    rs_poll_body_drain();
    if (s_nav_pending != 0u) {
        s_nav_pending = 0u;
        PanelUiBridge_GotoScreen(s_deferred_nav.screen_id, s_deferred_nav.action);
    }
    /* WARN до SYS_READY: к моменту снятия «ПРОВЕРКА» списки уже в Model/view. */
    if (s_warn_pending != 0u) {
        s_warn_pending = 0u;
        g_rs_panel_dbg.ui_warn_pending = 0u;
        PanelUiBridge_SetWarningStatus(s_deferred_warn.active,
                                       s_deferred_warn.n_items,
                                       s_deferred_warn.titles,
                                       s_deferred_warn.details);
    }
    if (s_sys_ready_pending != 0u) {
        s_sys_ready_pending = 0u;
        PanelUiBridge_NotifySysReady();
    }
    if (s_fire_pending != 0u) {
        s_fire_pending = 0u;
        g_rs_panel_dbg.ui_fire_pending = 0u;
        PanelUiBridge_SetFireStatus(s_deferred_fire.active,
                                    s_deferred_fire.mode,
                                    s_deferred_fire.remaining_s,
                                    s_deferred_fire.n_zones,
                                    s_deferred_fire.zone_names,
                                    s_deferred_fire.zone_modes,
                                    s_deferred_fire.zone_remaining);
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

static void rs_poll_rsp_flush(RsPanelEndpoint *endpoint)
{
    uint16_t plen;
    uint8_t seq;
    uint8_t payload[96];
    uint32_t primask;

    if (endpoint == 0 || s_pending_poll_rsp_valid == 0u) {
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_pending_poll_rsp_valid == 0u) {
        if (primask == 0u) {
            __enable_irq();
        }
        return;
    }
    plen = s_pending_poll_rsp_len;
    seq = s_pending_poll_rsp_seq;
    if (plen > (uint16_t)sizeof(payload)) {
        plen = (uint16_t)sizeof(payload);
    }
    if (plen > 0u) {
        memcpy(payload, s_pending_poll_rsp, plen);
    }
    s_pending_poll_rsp_valid = 0u;
    s_pending_poll_rsp_len = 0u;
    if (primask == 0u) {
        __enable_irq();
    }
    if (plen > 0u) {
        uint16_t frame_len = (uint16_t)(9u + plen);
        uint16_t wire_len = (uint16_t)(frame_len + 8u);
        uint8_t head[16];
        head[0] = 0xA5u;
        head[1] = 0x5Au;
        head[2] = (uint8_t)(4u + plen);
        head[3] = endpoint->panel_addr;
        head[4] = seq;
        head[5] = RS_BUS_FLAG_DIR;
        head[6] = RS_PANEL_RSP_POLL;
        RsPanelDebug_OnPollRsp(0u, 0u);
        RsPanelDebug_OnPollRspWire(plen, wire_len, head, 7u);
        rs_bus_send_frame(endpoint, endpoint->panel_addr, seq,
                          RS_BUS_FLAG_DIR, RS_PANEL_RSP_POLL,
                          payload, plen);
    }
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

static uint16_t rs_put_u32le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
    return 4u;
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
    case RS_PANEL_PROFILE_SET_RS_ADDR:
    case RS_PANEL_PROFILE_SET_PANEL_TYPE:
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

static void rs_send_activity(RsPanelEndpoint *endpoint)
{
    uint8_t payload[RS_PANEL_ACTIVITY_PAYLOAD_SIZE_UID];
    uint16_t pos = 0u;
    const DevicePanelConfig *cfg;

    if (endpoint == 0) {
        return;
    }
    cfg = PanelCfg_Get();
    payload[pos++] = (uint8_t)DEVICE_PANEL_TYPE; /* класс устройства = панель (30) */
    pos = (uint16_t)(pos + rs_put_u16le(&payload[pos], endpoint->state.caps.fw_ver));
    /* ACTIVITY.hw_id = формфактор PANEL_TYPE_1/2/3 (не путать с DEVICE_PANEL_TYPE). */
    pos = (uint16_t)(pos + rs_put_u16le(&payload[pos],
                                        (uint16_t)PANEL_TYPE_NORMALIZE(cfg->panel_type)));
    payload[pos++] = endpoint->state.caps.status;
    payload[pos++] = (uint8_t)(g_uptime_sec & 0xFFu);
    payload[pos++] = (uint8_t)((g_uptime_sec >> 8) & 0xFFu);
    payload[pos++] = (uint8_t)((g_uptime_sec >> 16) & 0xFFu);
    payload[pos++] = (uint8_t)((g_uptime_sec >> 24) & 0xFFu);
    pos = (uint16_t)(pos + rs_put_u32le(&payload[pos], cfg->uid0));
    pos = (uint16_t)(pos + rs_put_u32le(&payload[pos], cfg->uid1));
    pos = (uint16_t)(pos + rs_put_u32le(&payload[pos], cfg->uid2));
    rs_bus_send_frame(endpoint,
                      endpoint->panel_addr,
                      endpoint->next_tx_seq++,
                      RS_BUS_FLAG_DIR,
                      RS_PANEL_RSP_ACTIVITY,
                      payload,
                      pos);
}

static void rs_send_discover_rsp(RsPanelEndpoint *endpoint)
{
    uint8_t payload[RS_PANEL_DISCOVER_RSP_SIZE];
    uint16_t pos = 0u;
    const DevicePanelConfig *cfg;

    if (endpoint == 0) {
        return;
    }
    cfg = PanelCfg_Get();
    pos = (uint16_t)(pos + rs_put_u32le(&payload[pos], cfg->uid0));
    pos = (uint16_t)(pos + rs_put_u32le(&payload[pos], cfg->uid1));
    pos = (uint16_t)(pos + rs_put_u32le(&payload[pos], cfg->uid2));
    payload[pos++] = endpoint->panel_addr;
    payload[pos++] = (cfg->addr_assigned != 0u) ? RS_PANEL_DISCOVER_FLAG_ASSIGNED : 0u;
    rs_bus_send_frame(endpoint,
                      endpoint->panel_addr,
                      endpoint->next_tx_seq++,
                      RS_BUS_FLAG_DIR,
                      RS_PANEL_RSP_DISCOVER,
                      payload,
                      pos);
}

static void rs_apply_assign_by_uid(RsPanelEndpoint *endpoint, const uint8_t *payload, uint16_t len)
{
    RsPanelAssignByUidCmd cmd;
    const DevicePanelConfig *cfg;

    if (endpoint == 0 || payload == 0 || len < RS_PANEL_ASSIGN_BY_UID_SIZE) {
        return;
    }
    cmd.uid0 = rs_get_u32le(&payload[0]);
    cmd.uid1 = rs_get_u32le(&payload[4]);
    cmd.uid2 = rs_get_u32le(&payload[8]);
    cmd.new_addr = payload[12];

    cfg = PanelCfg_Get();
    if (cfg->uid0 != cmd.uid0 || cfg->uid1 != cmd.uid1 || cfg->uid2 != cmd.uid2) {
        return;
    }
    if (PanelCfg_IsValidRsAddr(cmd.new_addr) == 0u) {
        return;
    }
    g_panel_cfg.rs_addr = cmd.new_addr;
    g_panel_cfg.addr_assigned = 1u;
    PanelCfg_Save();
    endpoint->panel_addr = g_panel_cfg.rs_addr;
    PanelBoot_SetRsAddr(endpoint->panel_addr);
    rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, 0u);
}

static void rs_send_version(RsPanelEndpoint *endpoint)
{
    const char *ver;
    size_t n;

    if (endpoint == 0) {
        return;
    }
    ver = GetAppVersion();
    if (ver == 0) {
        return;
    }
    n = strlen(ver);
    if (n == 0u || n > 0xFFFFu) {
        return;
    }
    rs_bus_send_frame(endpoint,
                      endpoint->panel_addr,
                      endpoint->next_tx_seq++,
                      RS_BUS_FLAG_DIR,
                      RS_PANEL_CMD_BOOT_GET_VERSION,
                      (const uint8_t *)ver,
                      (uint16_t)n);
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
    /* До OnPollStream/FAULT: снять lost, иначе push_faults_ui no-op. */
    PanelLinkMonitor_OnHostRx();
    g_rs_panel_dbg.current_screen = endpoint->state.current_screen;

    endpoint->state.last_host_seq = frame->seq;

    switch (frame->cmd) {
    case RS_PANEL_CMD_CAPS_REQ:
        /* Как ACTIVITY: только unicast — иначе 2 панели dual-TX на half-duplex. */
        if (frame->addr != endpoint->panel_addr) {
            break;
        }
        rs_send_caps(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++);
        break;
    case RS_PANEL_CMD_POLL:
        {
            if (frame->addr != endpoint->panel_addr) {
                break; /* broadcast POLL → dual-TX; только свой addr */
            }
            if (frame->payload_len == 0u || frame->payload == 0 ||
                frame->payload[0] != RS_PANEL_V3_VERSION) {
                break; /* только v3 POLL */
            }
            /* Сначала ACK+stream (FAULT_CLEAR/SET, каталог), затем BuildRsp:
             * иначе faults_crc в RSP отстаёт на 1 POLL → мастер вечно
             * FAULT_CLEAR при mismatch, UI/журнал не догоняют. TX — из main. */
            (void)RsPanelV3Slave_OnPollAcks(frame->payload, frame->payload_len);
            (void)RsPanelV3Slave_OnPollStream(frame->payload, frame->payload_len);
            {
                uint8_t payload[96];
                uint16_t plen;
                /* Всегда полный BuildRsp (24 Б + EVT). Раньше SYS-only POLL
                 * (≤5 Б — норма при тишине/пожаре без дельты ZONES) давал
                 * RSP=ver(1) → DecodeRsp fail, START_SP/STOP не уходили до
                 * следующего TIME/ZONES (>5 Б) — задержка в секунды. */
                plen = RsPanelV3Slave_BuildRsp(payload, sizeof(payload));
                if (plen > 0u && plen <= (uint16_t)sizeof(s_pending_poll_rsp)) {
                    memcpy(s_pending_poll_rsp, payload, plen);
                    s_pending_poll_rsp_len = plen;
                    s_pending_poll_rsp_seq = endpoint->next_tx_seq++;
                    s_pending_poll_rsp_valid = 1u;
                }
            }
            rs_poll_body_enqueue(frame->payload, frame->payload_len);
        }
        break;
    case RS_PANEL_CMD_PROFILE_SET:
        {
            RsPanelProfileSetCmd profile_cmd;
            if (rs_decode_profile_set_cmd(frame->payload, frame->payload_len, &profile_cmd)) {
                /* CAPS/ответ на старом адресе, затем применяем новый rs_addr из Flash. */
                PanelState_ApplyProfileSet(&endpoint->state, &profile_cmd);
                rs_send_caps(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++);
                endpoint->panel_addr = PanelCfg_Get()->rs_addr;
                PanelBoot_SetRsAddr(endpoint->panel_addr);
            }
        }
        break;
    case RS_PANEL_CMD_PANEL_RESET:
        if (frame->addr != endpoint->panel_addr) {
            break;
        }
        PanelState_ResetUi(&endpoint->state);
        PanelUiBridge_GotoScreen(RS_PANEL_SCREEN_LOGO, RS_PANEL_UI_ACTION_REPLACE);
        rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        break;
    case RS_PANEL_CMD_ENTER_BOOTLOADER:
        if (frame->addr != endpoint->panel_addr) {
            break;
        }
        /* ACK из RX IRQ; reset — из PanelUpd_Timer10ms (не Delay/Reset в callback UART). */
        rs_send_ack(endpoint, endpoint->panel_addr, endpoint->next_tx_seq++, frame->seq);
        PanelUpd_RequestEnterBootloader(endpoint->panel_addr);
        break;
    case RS_PANEL_CMD_ACTIVITY_REQ:
        /* Только unicast на свой addr — broadcast дал бы dual-TX на half-duplex. */
        if (frame->addr == endpoint->panel_addr) {
            rs_send_activity(endpoint);
        }
        break;
    case RS_PANEL_CMD_DISCOVER:
        /* Ответ со случайной задержкой — из Timer10ms (не TX из RX IRQ пачкой). */
        endpoint->discover_pending = 1u;
        endpoint->discover_delay_ticks =
            (uint16_t)((PanelCfg_DiscoverDelayMs() + 9u) / 10u);
        if (endpoint->discover_delay_ticks == 0u) {
            endpoint->discover_delay_ticks = 1u;
        }
        break;
    case RS_PANEL_CMD_ASSIGN_BY_UID:
        rs_apply_assign_by_uid(endpoint, frame->payload, frame->payload_len);
        break;
    case RS_PANEL_CMD_BOOT_GET_VERSION:
        rs_send_version(endpoint);
        break;
    default:
        break;
    }
}

void RsPanelEndpoint_Init(void)
{
    uint8_t addr;

    memset(&g_endpoint, 0, sizeof(g_endpoint));
    RsPanelV3Slave_Init();
    RsPanelDebug_Reset();
    PanelLinkMonitor_Init();
    addr = PanelCfg_Init();
    g_endpoint.panel_addr = addr;
    PanelBoot_SetRsAddr(g_endpoint.panel_addr);
    g_endpoint.next_tx_seq = 1u;
    /* uptime_sec++ в Timer10ms; ACTIVITY только по ACTIVITY_REQ от ППКУ. */
    g_activity_accum_ms = 0u;
    PanelState_Init(&g_endpoint.state);
    /* OLED remap после чтения Flash (type3 → A0/C0). */
    Display_ApplyPanelType(PanelCfg_Get()->panel_type);
    RsBus_Init(&g_endpoint.bus, &huart4, BRP_485_EN_GPIO_Port, BRP_485_EN_Pin, rs_endpoint_on_frame, &g_endpoint);
}

void RsPanelEndpoint_Timer10ms(void)
{
    /* FE/ORE без ErrorCallback оставляли RX мёртвым — сторож поднимает DMA. */
    RsBus_RxWatchdog(&g_endpoint.bus);
    /* RSP сразу из thread-контекста — до тяжёлого UI/кнопок. */
    rs_poll_rsp_flush(&g_endpoint);
    /* v3 POLL body (ZONES/TIME) — не в UART IRQ; UI fire/warn — отдельно ниже. */
    rs_poll_body_drain();
    /* UI применяем из TouchGFX tick (после перехода logo→MAIN), а не отсюда:
     * иначе WARN попадает в уничтожаемый view, а setupScreen снова рисует «НОРМА». */
    PanelUpd_Timer10ms();
    if (g_endpoint.discover_pending != 0u) {
        if (g_endpoint.discover_delay_ticks > 0u) {
            g_endpoint.discover_delay_ticks--;
        }
        if (g_endpoint.discover_delay_ticks == 0u) {
            g_endpoint.discover_pending = 0u;
            rs_send_discover_rsp(&g_endpoint);
        }
    }
    PanelState_SampleButtons(&g_endpoint.state);
    RsPanelV3Slave_DrainPanelState(&g_endpoint.state);
    /* Monitor до Slave: иначе push_faults_ui в том же тике видит IsLost=1
     * и quiet-timer крутит dirty без смены WARN («НЕТ СВЯЗИ» залипает). */
    PanelLinkMonitor_Timer10ms();
    RsPanelV3Slave_Timer10ms();
    /* CAPS только по CAPS_REQ / PROFILE_SET — без незапрошенного TX. */
    (void)PanelState_TakeCapsResyncPending(&g_endpoint.state);
    g_rs_panel_dbg.current_screen = g_endpoint.state.current_screen;
    g_rs_panel_dbg.pending_ui_count = g_endpoint.state.pending_ui_count;
    g_rs_panel_dbg.pending_btn_count = g_endpoint.state.pending_btn_count;
    RsPanelDebug_Timer10ms(HAL_GetTick());
    g_rs_panel_dbg.rx_err_count = g_endpoint.bus.rx_err_count;
    g_rs_panel_dbg.rx_last_err_code = g_endpoint.bus.rx_last_err_code;
    g_rs_panel_dbg.rx_rearm_wd = g_endpoint.bus.rx_rearm_wd;
    g_rs_panel_dbg.panel_addr = g_endpoint.panel_addr;
    g_activity_accum_ms += 10u;
    if (g_activity_accum_ms >= 1000u) {
        g_activity_accum_ms = 0u;
        g_uptime_sec++;
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    static uint8_t s_rx_shadow[RS_BUS_RX_DMA_SIZE];
    uint16_t n;

    if (huart != &huart4 || size == 0u) {
        return;
    }
    n = size;
    if (n > (uint16_t)sizeof(s_rx_shadow)) {
        n = (uint16_t)sizeof(s_rx_shadow);
    }
    memcpy(s_rx_shadow, g_endpoint.bus.rx_dma_buf, n);
    /* Сразу re-arm: разбор+ответ в IRQ дольше байта на линии. */
    RsBus_RxRearmFromIsr(&g_endpoint.bus);
    RsPanelDebug_OnRxDma(n);
    {
        uint16_t i;
        g_rs_panel_dbg.rx_last_len = n;
        for (i = 0u; i < (uint16_t)sizeof(g_rs_panel_dbg.rx_last_head); i++) {
            g_rs_panel_dbg.rx_last_head[i] = (i < n) ? s_rx_shadow[i] : 0u;
        }
        for (i = 0u; i < n; i++) {
            if (s_rx_shadow[i] == 0xA5u) {
                g_rs_panel_dbg.rx_a5_seen++;
                break;
            }
        }
    }
    RsBus_ProcessRxBytes(&g_endpoint.bus, s_rx_shadow, n);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart4) {
        return;
    }
    /* Без этого первая FE на шине → HAL abort RX → панель глухая навсегда. */
    RsBus_RecoverFromError(&g_endpoint.bus);
}
