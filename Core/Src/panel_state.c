#include "panel_state.h"

#include <string.h>

#include "Display/display.h"
#include "menu_ui.h"
#include "panel_app.h"
#include "panel_cfg.h"
#include "rs_panel_debug.h"
#include "upd.h"

static void panel_state_fill_caps(PanelStateContext *ctx);
static void panel_state_promote_to_big(PanelStateContext *ctx);

static void panel_state_push_btn(PanelStateContext *ctx, uint8_t type, uint8_t state, uint8_t level)
{
    if (ctx == 0 || ctx->pending_btn_count >= RS_PANEL_MAX_POLL_BTN_EVENTS) {
        return;
    }
    ctx->pending_btn[ctx->pending_btn_count].type = type;
    ctx->pending_btn[ctx->pending_btn_count].state = state;
    ctx->pending_btn[ctx->pending_btn_count].level = level;
    ctx->pending_btn_count++;
}

static void panel_state_push_ui(PanelStateContext *ctx, uint8_t evt_type, uint16_t p1, uint16_t p2)
{
    if (ctx == 0 || ctx->pending_ui_count >= RS_PANEL_MAX_POLL_UI_EVENTS) {
        return;
    }

    ctx->pending_ui[ctx->pending_ui_count].evt_type = evt_type;
    ctx->pending_ui[ctx->pending_ui_count].p1 = p1;
    ctx->pending_ui[ctx->pending_ui_count].p2 = p2;
    ctx->pending_ui_count++;
    RsPanelDebug_OnUiEventQueued(evt_type, p1);
}

static uint8_t panel_state_btn_type(uint8_t local_button)
{
    switch (local_button) {
    case BUT_ESC:   return RS_PANEL_BTN_ESC;
    case BUT_UP:    return RS_PANEL_BTN_UP;
    case BUT_DOWN:  return RS_PANEL_BTN_DOWN;
    case BUT_ENTER: return RS_PANEL_BTN_ENTER;
    case BUT_STOP:  return RS_PANEL_BTN_STOP;
    case BUT_FIRE:  return RS_PANEL_BTN_START_SP;
    case BUT_FORCE: return RS_PANEL_BTN_START_ALL;
    default:        return 0u;
    }
}

static uint8_t panel_state_is_remote_journal_button(uint16_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_JOURNAL &&
        screen != RS_PANEL_SCREEN_MENU_JOURNAL_DETAIL) {
        return 0u;
    }

    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

static uint8_t panel_state_is_remote_menu_root_button(uint16_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_ROOT) {
        return 0u;
    }
    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

static uint8_t panel_state_is_remote_menu_back_button(uint16_t screen, uint8_t btn)
{
    if (btn != BUT_ESC) {
        return 0u;
    }

    /* Для подэкранов меню (connection/devices/config/...) кнопка ESC делегируется master'у,
     * чтобы RS-сессия экрана не “рассинхронизировалась” из-за локального gotoScreen(). */
    switch (screen) {
    case RS_PANEL_SCREEN_MENU_CONNECTION:
    case RS_PANEL_SCREEN_MENU_DEVICES:
    case RS_PANEL_SCREEN_MENU_DEVICE_DETAIL:
    case RS_PANEL_SCREEN_MENU_CONFIG:
    case RS_PANEL_SCREEN_MENU_SETTINGS:
    case RS_PANEL_SCREEN_MENU_BLOCK_ZONE:
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t panel_state_is_remote_device_button(uint16_t screen, uint8_t btn)
{
    switch (screen) {
    case RS_PANEL_SCREEN_MENU_DEVICES:
        return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
    case RS_PANEL_SCREEN_MENU_DEVICE_DETAIL:
        return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN) ? 1u : 0u;
    default:
        return 0u;
    }
}

static uint8_t panel_state_is_remote_block_zone_button(uint16_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_BLOCK_ZONE) {
        return 0u;
    }
    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

static uint8_t panel_state_is_remote_connection_button(uint16_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_CONNECTION) {
        return 0u;
    }
    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

/* Маршрут кнопок: сначала реальный TouchGFX menu-session, потом RS current_screen.
 * НИКОГДА не подменяем MAIN/LOGO на MENU_ROOT: иначе UP на главном уходит как UI_NAV
 * меню, мастер тихо ставит MENU_ROOT без UI_NAV на панель → рассинхрон. */
static uint16_t panel_state_route_screen(uint16_t screen)
{
    const uint16_t session = MenuUi_GetMenuSessionScreen();
    if (session != 0u) {
        return session;
    }

    if (MenuUi_IsMainScreenActive() != 0u) {
        return RS_PANEL_SCREEN_MAIN;
    }

    switch (screen) {
    case RS_PANEL_SCREEN_MENU_JOURNAL:
    case RS_PANEL_SCREEN_MENU_JOURNAL_DETAIL:
    case RS_PANEL_SCREEN_MENU_DEVICES:
    case RS_PANEL_SCREEN_MENU_DEVICE_DETAIL:
    case RS_PANEL_SCREEN_MENU_CONNECTION:
    case RS_PANEL_SCREEN_MENU_CONFIG:
    case RS_PANEL_SCREEN_MENU_BLOCK_ZONE:
    case RS_PANEL_SCREEN_MENU_SETTINGS:
    case RS_PANEL_SCREEN_MENU_SOUND:
    case RS_PANEL_SCREEN_MENU_ROOT:
    case RS_PANEL_SCREEN_MAIN:
    case RS_PANEL_SCREEN_LOGO:
        return screen;
    default:
        return screen;
    }
}

static void panel_state_push_journal_ui_event(PanelStateContext *ctx, uint8_t btn)
{
    if (ctx == 0) {
        return;
    }

    switch (btn) {
    case BUT_ESC:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_BACK, 0u, 0u);
        break;
    case BUT_UP:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 0u, 0u);
        break;
    case BUT_DOWN:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 1u, 0u);
        break;
    case BUT_ENTER:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_CONFIRM, 0u, 0u);
        break;
    default:
        break;
    }
}

static void panel_state_push_menu_root_ui_event(PanelStateContext *ctx, uint8_t btn)
{
    if (ctx == 0) {
        return;
    }

    const uint16_t sel = MenuUi_GetMenuSelected();

    switch (btn) {
    case BUT_ESC:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_BACK, 0u, 0u);
        break;
    case BUT_UP:
        /* direction: 0=UP, 1=DOWN */
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 0u, sel);
        break;
    case BUT_DOWN:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 1u, sel);
        break;
    case BUT_ENTER:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_MENU_SELECT, sel, 0u);
        break;
    default:
        break;
    }
}

static void panel_state_push_device_ui_event(PanelStateContext *ctx, uint8_t btn)
{
    if (ctx == 0) {
        return;
    }

    switch (btn) {
    case BUT_ESC:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_BACK, 0u, 0u);
        break;
    case BUT_UP:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 0u, 0u);
        break;
    case BUT_DOWN:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 1u, 0u);
        break;
    case BUT_ENTER:
        if (ctx->current_screen == RS_PANEL_SCREEN_MENU_DEVICES) {
            panel_state_push_ui(ctx, RS_PANEL_UI_EVT_CONFIRM, 0u, 0u);
        }
        break;
    default:
        break;
    }
}

static void panel_state_push_block_zone_ui_event(PanelStateContext *ctx, uint8_t btn)
{
    if (ctx == 0) {
        return;
    }

    switch (btn) {
    case BUT_ESC:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_BACK, 0u, 0u);
        break;
    case BUT_UP:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 0u, 0u);
        break;
    case BUT_DOWN:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 1u, 0u);
        break;
    case BUT_ENTER:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_CONFIRM, 0u, 0u);
        break;
    default:
        break;
    }
}

static void panel_state_push_connection_ui_event(PanelStateContext *ctx, uint8_t btn)
{
    if (ctx == 0) {
        return;
    }

    switch (btn) {
    case BUT_ESC:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_BACK, 0u, 0u);
        break;
    case BUT_UP:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 0u, 0u);
        break;
    case BUT_DOWN:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_NAV, 1u, 0u);
        break;
    case BUT_ENTER:
        panel_state_push_ui(ctx, RS_PANEL_UI_EVT_CONFIRM, 0u, 0u);
        break;
    default:
        break;
    }
}

static PanelStateContext *s_active_ctx;

void PanelState_Init(PanelStateContext *ctx)
{
    if (ctx == 0) {
        return;
    }
    s_active_ctx = ctx;
    memset(ctx, 0, sizeof(*ctx));
    panel_state_fill_caps(ctx);
    ctx->current_screen = RS_PANEL_SCREEN_LOGO;
    ctx->fire_active = 0u;
}

void PanelState_QueueUiEvent(uint8_t evt_type, uint16_t p1, uint16_t p2)
{
    if (s_active_ctx == 0) {
        return;
    }
    panel_state_push_ui(s_active_ctx, evt_type, p1, p2);
}

static void panel_state_fill_caps(PanelStateContext *ctx)
{
    static const uint8_t btns_big[] = {
        RS_PANEL_BTN_ESC,
        RS_PANEL_BTN_UP,
        RS_PANEL_BTN_DOWN,
        RS_PANEL_BTN_ENTER,
        RS_PANEL_BTN_STOP,
        RS_PANEL_BTN_START_SP,
        RS_PANEL_BTN_START_ALL
    };
    static const uint8_t btns_small[] = {
        RS_PANEL_BTN_ESC,
        RS_PANEL_BTN_UP,
        RS_PANEL_BTN_DOWN,
        RS_PANEL_BTN_ENTER,
        RS_PANEL_BTN_STOP,
        RS_PANEL_BTN_START_SP
    };
    static const uint8_t leds_big[] = {
        RS_PANEL_LED_POWER,
        RS_PANEL_LED_NORM,
        RS_PANEL_LED_START,
        RS_PANEL_LED_STOP,
        RS_PANEL_LED_ERR,
        RS_PANEL_LED_FIRE,
        RS_PANEL_LED_AUTO_OFF,
        RS_PANEL_LED_BUT_START_ALL,
        RS_PANEL_LED_BUT_STOP,
        RS_PANEL_LED_BUT_START_SP,
        RS_PANEL_LED_BUT_ENTER,
        RS_PANEL_LED_BUT_ESC,
        RS_PANEL_LED_LBL_START_ALL,
        RS_PANEL_LED_LBL_STOP,
        RS_PANEL_LED_LBL_START_SP
    };
    static const uint8_t leds_small[] = {
        RS_PANEL_LED_START,
        RS_PANEL_LED_FIRE,
        RS_PANEL_LED_AUTO_OFF,
        RS_PANEL_LED_BUT_STOP,
        RS_PANEL_LED_BUT_START_SP,
        RS_PANEL_LED_BUT_ENTER,
        RS_PANEL_LED_BUT_ESC,
        RS_PANEL_LED_LBL_STOP,
        RS_PANEL_LED_LBL_START_SP
    };
    const DevicePanelConfig *pcfg;
    uint8_t is_small;

    if (ctx == 0) {
        return;
    }
    pcfg = PanelCfg_Get();
    is_small = PANEL_TYPE_IS_SMALL(pcfg->panel_type) ? 1u : 0u;

    ctx->caps.fw_ver = (uint16_t)GetAppVersionU32();
    ctx->caps.hw_id = 1u;
    ctx->caps.ui_profile = PANEL_TYPE_NORMALIZE(pcfg->panel_type);
    ctx->caps.disp_w = 128u;
    ctx->caps.disp_h = 64u;
    if (is_small != 0u) {
        ctx->caps.btn_count = (uint8_t)(sizeof(btns_small) / sizeof(btns_small[0]));
        memcpy(ctx->caps.btn_list, btns_small, sizeof(btns_small));
        ctx->caps.led_count = (uint8_t)(sizeof(leds_small) / sizeof(leds_small[0]));
        memcpy(ctx->caps.led_list, leds_small, sizeof(leds_small));
    } else {
        ctx->caps.btn_count = (uint8_t)(sizeof(btns_big) / sizeof(btns_big[0]));
        memcpy(ctx->caps.btn_list, btns_big, sizeof(btns_big));
        ctx->caps.led_count = (uint8_t)(sizeof(leds_big) / sizeof(leds_big[0]));
        memcpy(ctx->caps.led_list, leds_big, sizeof(leds_big));
    }
    ctx->caps.flags = 0x03u;
    ctx->caps.status = 0x03u;
    ctx->caps.orientation = pcfg->orientation;
    ctx->caps.journal_lines = (pcfg->journal_lines != 0u) ? pcfg->journal_lines : 1u;
}

static void panel_state_promote_to_big(PanelStateContext *ctx)
{
    if (ctx == 0 || PANEL_TYPE_IS_BIG(g_panel_cfg.panel_type)) {
        return;
    }
    g_panel_cfg.panel_type = PANEL_TYPE_1;
    /* START_ALL = RsBtnType 0x07 → бит 6 */
    g_panel_cfg.btn_enable = (uint8_t)(g_panel_cfg.btn_enable | (uint8_t)(1u << 6));
    PanelCfg_Save();
    panel_state_fill_caps(ctx);
    Display_ApplyPanelType(g_panel_cfg.panel_type);
    ctx->caps_resync_pending = 1u;
}

uint8_t PanelState_TakeCapsResyncPending(PanelStateContext *ctx)
{
    uint8_t v;
    if (ctx == 0) {
        return 0u;
    }
    v = ctx->caps_resync_pending;
    ctx->caps_resync_pending = 0u;
    return v;
}

void PanelState_ResetUi(PanelStateContext *ctx)
{
    if (ctx == 0) {
        return;
    }
    ctx->current_screen = RS_PANEL_SCREEN_LOGO;
    ctx->fire_active = 0u;
    memset(ctx->warning_titles, 0, sizeof(ctx->warning_titles));
    memset(ctx->warning_details, 0, sizeof(ctx->warning_details));
    ctx->pending_btn_count = 0u;
    ctx->pending_ui_count = 0u;
}

void PanelState_ApplyProfileSet(PanelStateContext *ctx, const RsPanelProfileSetCmd *cmd)
{
    uint8_t need_save = 0u;

    if (ctx == 0 || cmd == 0) {
        return;
    }

    switch (cmd->sub) {
    case RS_PANEL_PROFILE_SET_ORIENTATION:
        ctx->caps.orientation = cmd->value.orientation;
        g_panel_cfg.orientation = cmd->value.orientation;
        need_save = 1u;
        break;
    case RS_PANEL_PROFILE_SET_BTN_MASK:
        g_panel_cfg.btn_enable = cmd->value.btn_enable;
        need_save = 1u;
        break;
    case RS_PANEL_PROFILE_SET_LED_MASK:
        g_panel_cfg.led_enable = cmd->value.led_enable;
        need_save = 1u;
        break;
    case RS_PANEL_PROFILE_SET_JOURNAL_LINES:
        ctx->caps.journal_lines = cmd->value.journal_lines;
        if (ctx->caps.journal_lines == 0u) {
            ctx->caps.journal_lines = 1u;
        }
        g_panel_cfg.journal_lines = ctx->caps.journal_lines;
        need_save = 1u;
        break;
    case RS_PANEL_PROFILE_SET_RS_ADDR:
        if (PanelCfg_IsValidRsAddr(cmd->value.rs_addr) != 0u) {
            g_panel_cfg.rs_addr = cmd->value.rs_addr;
            g_panel_cfg.addr_assigned = 1u;
            need_save = 1u;
        }
        break;
    case RS_PANEL_PROFILE_SET_PANEL_TYPE:
        if (cmd->value.panel_type == PANEL_TYPE_1 ||
            cmd->value.panel_type == PANEL_TYPE_2 ||
            cmd->value.panel_type == PANEL_TYPE_3) {
            g_panel_cfg.panel_type = cmd->value.panel_type;
            /* btn_enable: бит N = RsBtnType (N+1); START_ALL=0x07 → бит 6. */
            if (PANEL_TYPE_IS_SMALL(g_panel_cfg.panel_type)) {
                g_panel_cfg.btn_enable = (uint8_t)(g_panel_cfg.btn_enable & (uint8_t)~(1u << 6));
            } else {
                g_panel_cfg.btn_enable = (uint8_t)(g_panel_cfg.btn_enable | (uint8_t)(1u << 6));
            }
            PanelState_Init(ctx);
            Display_ApplyPanelType(g_panel_cfg.panel_type);
            need_save = 1u;
        }
        break;
    case RS_PANEL_PROFILE_SET_FACTORY_RESET:
        PanelCfg_SetDefaults(&g_panel_cfg);
        PanelState_Init(ctx);
        need_save = 1u;
        break;
    default:
        break;
    }

    if (need_save != 0u) {
        PanelCfg_Save();
    }
}

void PanelState_SampleButtons(PanelStateContext *ctx)
{
    uint16_t screen;
    uint8_t local_buttons[] = { BUT_ESC, BUT_UP, BUT_DOWN, BUT_ENTER, BUT_STOP, BUT_FIRE, BUT_FORCE };
    uint8_t i;
    uint8_t btn;
    uint8_t type;
    uint8_t level;
    uint8_t state;
    uint8_t is_small;

    if (ctx == 0) {
        return;
    }

    is_small = PANEL_TYPE_IS_SMALL(PanelCfg_Get()->panel_type) ? 1u : 0u;

    /* TouchGFX сам переходит logo→main. Синхронизируем только LOGO→MAIN:
     * если затирать MENU_* (окно UI_NAV меню → ещё не сработал GotoScreen),
     * UP/DOWN/ESC уходят как btn_event, а не как UI_NAV/BACK. */
    if (MenuUi_IsMainScreenActive() != 0u &&
        ctx->current_screen == RS_PANEL_SCREEN_LOGO) {
        ctx->current_screen = RS_PANEL_SCREEN_MAIN;
    }

    {
        const uint16_t session = MenuUi_GetMenuSessionScreen();
        if (session != 0u) {
            ctx->current_screen = session;
        }
    }

    screen = panel_state_route_screen(ctx->current_screen);

    ctx->caps.status = 0x02u;
    for (i = 0u; i < (uint8_t)(sizeof(local_buttons) / sizeof(local_buttons[0])); i++) {
        btn = local_buttons[i];
        type = panel_state_btn_type(btn);
        state = (uint8_t)Button_GetState(btn);
        level = (state == (uint8_t)ButtonStateReset || state == (uint8_t)ButtonStateError) ? 0u : 1u;

        if (state != (uint8_t)ButtonStateError) {
            ctx->caps.status |= 0x01u;
        }

        /* Автоопределение: тип 0 (малая) + нажатие ПУСК ОБЩИЙ → тип 1 (большая) + Flash. */
        if (btn == BUT_FORCE && is_small != 0u && level != 0u &&
            ctx->btn_prev_level[btn] == 0u) {
            panel_state_promote_to_big(ctx);
            is_small = 0u;
        }

        /* Пока тип малый — события ПУСК ОБЩИЙ на хост не шлём (кнопки «нет»). */
        if (is_small != 0u && btn == BUT_FORCE) {
            ctx->btn_prev_state[btn] = state;
            ctx->btn_prev_level[btn] = level;
            continue;
        }

        if (type != 0u &&
            (state != ctx->btn_prev_state[btn] || level != ctx->btn_prev_level[btn])) {
            uint8_t route_to_ui = panel_state_is_remote_journal_button(screen, btn);
            uint8_t route_device_ui = panel_state_is_remote_device_button(screen, btn);
            uint8_t route_block_zone_ui = panel_state_is_remote_block_zone_button(screen, btn);
            uint8_t route_connection_ui = panel_state_is_remote_connection_button(screen, btn);
            if (state == (uint8_t)ButtonStatePress) {
                const uint8_t on_main_screen = (MenuUi_IsMainScreenActive() != 0u);
                const uint8_t is_main_enter =
                    (on_main_screen != 0u && btn == BUT_ENTER && ctx->fire_active == 0u);
                const uint8_t is_menu_root = panel_state_is_remote_menu_root_button(screen, btn);

                if (is_main_enter != 0u) {
                    panel_state_push_ui(ctx, RS_PANEL_UI_EVT_CONFIRM, 0u, 0u);
                } else if (route_to_ui != 0u) {
                    panel_state_push_journal_ui_event(ctx, btn);
                } else if (route_device_ui != 0u) {
                    panel_state_push_device_ui_event(ctx, btn);
                } else if (route_block_zone_ui != 0u) {
                    panel_state_push_block_zone_ui_event(ctx, btn);
                } else if (route_connection_ui != 0u) {
                    panel_state_push_connection_ui_event(ctx, btn);
                } else if (is_menu_root != 0u) {
                    panel_state_push_menu_root_ui_event(ctx, btn);
                } else if (panel_state_is_remote_menu_back_button(screen, btn) != 0u) {
                    panel_state_push_ui(ctx, RS_PANEL_UI_EVT_BACK, 0u, 0u);
                } else {
                    panel_state_push_btn(ctx, type, state, level);
                }
            } else {
                panel_state_push_btn(ctx, type, state, level);
            }
        }

        ctx->btn_prev_state[btn] = state;
        ctx->btn_prev_level[btn] = level;
    }
}

void PanelState_FillPollResponse(PanelStateContext *ctx, RsPanelPollRsp *rsp)
{
    static const uint8_t level_btns[] = { BUT_FORCE, BUT_FIRE, BUT_STOP };
    uint8_t bi;

    if (ctx == 0 || rsp == 0) {
        return;
    }
    memset(rsp, 0, sizeof(*rsp));
    rsp->status = ctx->caps.status;
    rsp->evt_count = ctx->pending_btn_count;
    memcpy(rsp->btn_events, ctx->pending_btn, (size_t)ctx->pending_btn_count * sizeof(ctx->pending_btn[0]));
    rsp->ui_evt_count = ctx->pending_ui_count;
    memcpy(rsp->ui_events, ctx->pending_ui, (size_t)ctx->pending_ui_count * sizeof(ctx->pending_ui[0]));
    ctx->pending_btn_count = 0u;
    ctx->pending_ui_count = 0u;

    /* Абсолютный level пусковых кнопок в каждом POLL — живой Button_GetState.
     * Не перезаписываем уже лежащий в pending edge (Press): иначе POLL до
     * следующего Button_Process может обнулить только что зафиксированное нажатие. */
    for (bi = 0u; bi < (uint8_t)(sizeof(level_btns) / sizeof(level_btns[0])); bi++) {
        uint8_t btn = level_btns[bi];
        uint8_t type;
        uint8_t state;
        uint8_t level;
        uint8_t already = 0u;
        uint8_t ei;

        if (PANEL_TYPE_IS_SMALL(PanelCfg_Get()->panel_type) && btn == BUT_FORCE) {
            continue;
        }
        type = panel_state_btn_type(btn);
        if (type == 0u) {
            continue;
        }
        for (ei = 0u; ei < rsp->evt_count && ei < RS_PANEL_MAX_POLL_BTN_EVENTS; ei++) {
            if (rsp->btn_events[ei].type == type) {
                already = 1u;
                break;
            }
        }
        if (already != 0u) {
            continue;
        }
        state = (uint8_t)Button_GetState(btn);
        level = (state == (uint8_t)ButtonStateReset || state == (uint8_t)ButtonStateError) ? 0u : 1u;
        ctx->btn_prev_state[btn] = state;
        ctx->btn_prev_level[btn] = level;
        if (rsp->evt_count >= RS_PANEL_MAX_POLL_BTN_EVENTS) {
            break;
        }
        rsp->btn_events[rsp->evt_count].type = type;
        rsp->btn_events[rsp->evt_count].state = state;
        rsp->btn_events[rsp->evt_count].level = level;
        rsp->evt_count++;
    }
}
