#include "panel_state.h"

#include <string.h>

#include "menu_ui.h"
#include "rs_panel_debug.h"

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

static uint8_t panel_state_is_remote_journal_button(uint8_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_JOURNAL &&
        screen != RS_PANEL_SCREEN_MENU_JOURNAL_DETAIL) {
        return 0u;
    }

    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

static uint8_t panel_state_is_remote_menu_root_button(uint8_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_ROOT) {
        return 0u;
    }
    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

static uint8_t panel_state_is_remote_menu_back_button(uint8_t screen, uint8_t btn)
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

static uint8_t panel_state_is_remote_device_button(uint8_t screen, uint8_t btn)
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

static uint8_t panel_state_is_remote_block_zone_button(uint8_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_BLOCK_ZONE) {
        return 0u;
    }
    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
}

static uint8_t panel_state_is_remote_connection_button(uint8_t screen, uint8_t btn)
{
    if (screen != RS_PANEL_SCREEN_MENU_CONNECTION) {
        return 0u;
    }
    return (btn == BUT_ESC || btn == BUT_UP || btn == BUT_DOWN || btn == BUT_ENTER) ? 1u : 0u;
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

void PanelState_Init(PanelStateContext *ctx)
{
    static const uint8_t btns[] = {
        RS_PANEL_BTN_ESC,
        RS_PANEL_BTN_UP,
        RS_PANEL_BTN_DOWN,
        RS_PANEL_BTN_ENTER,
        RS_PANEL_BTN_STOP,
        RS_PANEL_BTN_START_SP,
        RS_PANEL_BTN_START_ALL
    };
    static const uint8_t leds[] = {
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

    if (ctx == 0) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->caps.fw_ver = 1u;
    ctx->caps.hw_id = 1u;
    ctx->caps.disp_w = 128u;
    ctx->caps.disp_h = 64u;
    ctx->caps.journal_lines = 1u;
    ctx->caps.btn_count = (uint8_t)(sizeof(btns) / sizeof(btns[0]));
    memcpy(ctx->caps.btn_list, btns, sizeof(btns));
    ctx->caps.led_count = (uint8_t)(sizeof(leds) / sizeof(leds[0]));
    memcpy(ctx->caps.led_list, leds, sizeof(leds));
    ctx->caps.flags = 0x03u;
    ctx->caps.status = 0x03u;
    ctx->current_screen = RS_PANEL_SCREEN_LOGO;
    ctx->fire_active = 0u;
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
    if (ctx == 0 || cmd == 0) {
        return;
    }

    switch (cmd->sub) {
    case RS_PANEL_PROFILE_SET_ORIENTATION:
        ctx->caps.orientation = cmd->value.orientation;
        break;
    case RS_PANEL_PROFILE_SET_JOURNAL_LINES:
        ctx->caps.journal_lines = cmd->value.journal_lines;
        if (ctx->caps.journal_lines == 0u) {
            ctx->caps.journal_lines = 1u;
        }
        break;
    case RS_PANEL_PROFILE_SET_FACTORY_RESET:
        PanelState_Init(ctx);
        break;
    default:
        break;
    }
}

void PanelState_SampleButtons(PanelStateContext *ctx)
{
    uint8_t local_buttons[] = { BUT_ESC, BUT_UP, BUT_DOWN, BUT_ENTER, BUT_STOP, BUT_FIRE, BUT_FORCE };
    uint8_t i;
    uint8_t btn;
    uint8_t type;
    uint8_t level;
    uint8_t state;

    if (ctx == 0) {
        return;
    }

    /* TouchGFX сам переходит logo→main; RS current_screen обновляется только по UI_NAV. */
    if (MenuUi_IsMainScreenActive() != 0u && ctx->current_screen != RS_PANEL_SCREEN_MAIN) {
        ctx->current_screen = RS_PANEL_SCREEN_MAIN;
    }

    ctx->caps.status = 0x02u;
    for (i = 0u; i < (uint8_t)(sizeof(local_buttons) / sizeof(local_buttons[0])); i++) {
        btn = local_buttons[i];
        type = panel_state_btn_type(btn);
        state = (uint8_t)Button_GetState(btn);
        level = (state == (uint8_t)ButtonStateReset || state == (uint8_t)ButtonStateError) ? 0u : 1u;

        if (state != (uint8_t)ButtonStateError) {
            ctx->caps.status |= 0x01u;
        }

        if (type != 0u &&
            (state != ctx->btn_prev_state[btn] || level != ctx->btn_prev_level[btn])) {
            uint8_t route_to_ui = panel_state_is_remote_journal_button(ctx->current_screen, btn);
            uint8_t route_device_ui = panel_state_is_remote_device_button(ctx->current_screen, btn);
            uint8_t route_block_zone_ui = panel_state_is_remote_block_zone_button(ctx->current_screen, btn);
            uint8_t route_connection_ui = panel_state_is_remote_connection_button(ctx->current_screen, btn);
            if (state == (uint8_t)ButtonStatePress) {
                const uint8_t on_main_screen =
                    (ctx->current_screen == RS_PANEL_SCREEN_MAIN || MenuUi_IsMainScreenActive() != 0u);
                const uint8_t is_main_enter =
                    (on_main_screen != 0u && btn == BUT_ENTER && ctx->fire_active == 0u);
                const uint8_t is_menu_root = panel_state_is_remote_menu_root_button(ctx->current_screen, btn);

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
                } else if (panel_state_is_remote_menu_back_button(ctx->current_screen, btn) != 0u) {
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
}
