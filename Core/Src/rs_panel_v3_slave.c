#include "rs_panel_v3_slave.h"

#include "rs_panel_protocol.h"
#include "button.h"
#include "led.h"
#include "beeper.h"
#include "sound_profiles.h"
#include "panel_ui_bridge.h"
#include "panel_journal_cache.h"
#include "panel_link_monitor.h"
#include "rs_panel_endpoint.h"
#include "menu_ui.h"
#include "esp_manager.h"
#include "device_config.h"
#include "panel_host_cache.h"
#include "panel_catalog_cache.h"
#include "fire.h"
#include "rtc_cache.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define RS_V3_HOLD_START_ALL_MS  3000u
#define RS_V3_HOLD_FIRE_RESET_MS 5000u
#define RS_V3_BLINK_HALF_MS      800u
/* После последнего ZONE/DEV TLV — сохранить Flash (не на каждый POLL). */
#define RS_V3_CATALOG_FLASH_QUIET_MS 400u

static uint8_t s_sys_ready;
static uint8_t s_fire_active;
static uint8_t s_config_active;
static uint8_t s_has_faults;
static uint8_t s_power_input_fault; /* SYS POWER_INPUT_FAULT → мигание LED_POWER */
static uint16_t s_led_power_toggle_cnt; /* тики 10 мс; период = LED_POWER_TOOGLE_PERIOD_MS */

static RsPanelV3Event s_event;
static uint8_t s_event_seq_ctr;

static char s_zone_name_by_id[32 + 1u][RS_PANEL_V3_ZONE_NAME_LEN];
static uint8_t s_zone_name_valid[32 + 1u];
static RsPanelV3DeviceItem s_device_catalog[RS_PANEL_V3_MAX_CATALOG_MCU];
static uint16_t s_device_catalog_count;
static RsPanelV3FaultEvtItem s_active_faults[RS_PANEL_V3_MAX_FAULTS];
static uint16_t s_active_fault_count;
static uint16_t s_zones_crc;
static uint16_t s_devices_crc;
static uint16_t s_faults_crc;
static uint8_t s_catalog_ready;
static uint8_t s_rsp_flags;
static uint8_t s_catalog_flash_dirty;
static uint32_t s_catalog_flash_dirty_ms;

static RsPanelV3Zones s_zones;
static uint8_t s_zones_valid;

static uint16_t s_hold_start_all_ms;
static uint8_t s_hold_start_all;
static uint8_t s_hold_start_all_committed;
static uint16_t s_hold_reset_ms;
static uint8_t s_hold_reset;
static uint8_t s_hold_reset_committed;
static uint8_t s_blink_on;
static uint32_t s_blink_last_ms;
static uint8_t s_local_timer_s[RS_PANEL_V3_MAX_ZONES];
static uint32_t s_local_timer_tick_ms;
static uint8_t s_v3_active;
static uint8_t s_selected_zone; /* 0 = все; CAN zone 1..N */
static uint8_t s_fault_placeholder_active;
static uint8_t s_fault_ui_dirty;
static uint32_t s_fault_ui_dirty_ms;
#define RS_V3_FAULT_UI_QUIET_MS 250u

/* Звук неисправности (v3: CMD_SOUND с ППКУ нет) — SIGNAL→DUTY как на ППКУ. */
#define RS_V3_FS_IDLE       0u
#define RS_V3_FS_WAIT_DUTY  1u
#define RS_V3_FS_DUTY       2u
#define RS_V3_FAULT_SIG_MS  ((SOUND_FAULT_SIGNAL_ON_MS + SOUND_FAULT_SIGNAL_OFF_MS) * SOUND_FAULT_SIGNAL_PULSES)
#define RS_V3_ATTN_SIG_MS   ((SOUND_ATTN_SIGNAL_ON_MS + SOUND_ATTN_SIGNAL_OFF_MS) * SOUND_ATTN_SIGNAL_PULSES)

static uint8_t s_fault_sound_phase;   /* IDLE / WAIT_DUTY / DUTY */
static uint8_t s_fault_sound_resync;  /* 1 = после CLEAR_ALL→SET, звук в конце */
static uint32_t s_fault_sound_deadline_ms;
static uint32_t s_fault_resync_wait_ms; /* старт ожидания SET после CLEAR_ALL */
static uint16_t s_fault_ui_pushed_count; /* последний n, ушедший в UI */
static uint16_t s_fault_pre_clear_count; /* count до CLEAR_ALL (для SIGNAL при росте) */

/* Локальные фазы звука/LED пожара (v3: CMD_SOUND/LED с хоста нет). */
#define RS_V3_FIRE_SND_IDLE   0u
#define RS_V3_FIRE_SND_ALERT  1u  /* сигнал ПОЖАР */
#define RS_V3_FIRE_SND_START  2u  /* тушение → паттерн ПУСК + LED_START */
#define RS_V3_FIRE_SND_DUTY   3u  /* ТУШ.ВЫП / дежурный */
static uint8_t s_fire_snd_phase;

/* Общая очередь EVT (журнал/меню/wifi/звук + fire-ctrl).
 * s_event = «1-е место» (в полёте до ACK). s_evt_q = ожидающие.
 * Fire-ctrl вставляется после последнего fire в очереди (2-е / 2+ место),
 * обычные — в хвост. In-flight не вытесняем. */
#define RS_V3_EVT_Q_MAX 12u
static RsPanelV3Event s_evt_q[RS_V3_EVT_Q_MAX];
static uint8_t s_evt_q_n;
/* Повтор PostEvent после hold, пока слот/очередь не приняли. */
static uint8_t s_start_all_evt_pending;
static uint8_t s_fire_reset_evt_pending;

static void rs_v3_restore_start_all_idle_leds(void);
static uint8_t rs_v3_has_attention_faults(void);
static void rs_v3_fault_beep(uint8_t attn, uint8_t signal);
static void rs_v3_fault_sound_after_resync(void);
static void rs_v3_sync_fire_indication(void);
static void rs_v3_evt_flush_queue(void);
static void rs_v3_evt_retry_sticky(void);
static void rs_v3_recalc_catalog_crc(void);
static uint8_t rs_v3_catalog_flash_load(void);

static uint32_t rs_v3_get_u32le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

uint8_t RsPanelV3Slave_IsV3Active(void)
{
    return s_v3_active;
}

void RsPanelV3Slave_Init(void)
{
    memset(&s_event, 0, sizeof(s_event));
    memset(s_zone_name_by_id, 0, sizeof(s_zone_name_by_id));
    memset(s_zone_name_valid, 0, sizeof(s_zone_name_valid));
    memset(s_device_catalog, 0, sizeof(s_device_catalog));
    memset(s_active_faults, 0, sizeof(s_active_faults));
    memset(&s_zones, 0, sizeof(s_zones));
    memset(s_local_timer_s, 0, sizeof(s_local_timer_s));
    s_device_catalog_count = 0u;
    s_active_fault_count = 0u;
    s_zones_crc = 0xFFFFu;
    s_devices_crc = 0xFFFFu;
    s_faults_crc = 0xFFFFu;
    s_catalog_ready = 0u;
    s_catalog_flash_dirty = 0u;
    s_catalog_flash_dirty_ms = 0u;
    s_rsp_flags = RS_PANEL_V3_RSP_FLAG_NEED_CATALOG;
    s_sys_ready = 0u;
    s_fire_active = 0u;
    s_config_active = 0u;
    s_has_faults = 0u;
    s_power_input_fault = 0u;
    s_led_power_toggle_cnt = 0u;
    s_event_seq_ctr = 1u;
    s_hold_start_all = 0u;
    s_hold_reset = 0u;
    s_zones_valid = 0u;
    s_v3_active = 0u;
    s_selected_zone = 0u;
    s_fault_placeholder_active = 0u;
    s_fault_ui_dirty = 0u;
    s_fault_ui_dirty_ms = 0u;
    s_fault_sound_phase = RS_V3_FS_IDLE;
    s_fault_sound_deadline_ms = 0u;
    s_fault_sound_resync = 0u;
    s_fault_resync_wait_ms = 0u;
    s_fault_ui_pushed_count = 0u;
    s_fault_pre_clear_count = 0u;
    s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
    s_evt_q_n = 0u;
    memset(s_evt_q, 0, sizeof(s_evt_q));
    s_start_all_evt_pending = 0u;
    s_fire_reset_evt_pending = 0u;
    /* Flash-каталог: при совпадении CRC с ППКУ rewrite пропускается. */
    if (rs_v3_catalog_flash_load() == 0u) {
        s_rsp_flags |= RS_PANEL_V3_RSP_FLAG_NEED_CATALOG;
        s_catalog_ready = 0u;
        rs_v3_recalc_catalog_crc();
    }
    /* Надпись ПУСК ОБЩИЙ должна гореть в дежурном режиме с включения. */
    rs_v3_restore_start_all_idle_leds();
}

uint8_t RsPanelV3Slave_IsSysReady(void) { return s_sys_ready; }
uint8_t RsPanelV3Slave_HasFaults(void)
{
    return (s_has_faults != 0u || s_active_fault_count > 0u) ? 1u : 0u;
}
uint8_t RsPanelV3Slave_IsFireActive(void) { return s_fire_active; }
uint8_t RsPanelV3Slave_IsConfigActive(void) { return s_config_active; }
uint8_t RsPanelV3Slave_IsHoldActive(void) { return s_hold_start_all; }

uint8_t RsPanelV3Slave_GetHoldRemainingSec(void)
{
    uint16_t left_ms;
    if (s_hold_start_all == 0u || s_hold_start_all_committed != 0u) {
        return 0u;
    }
    if (s_hold_start_all_ms >= RS_V3_HOLD_START_ALL_MS) {
        return 0u;
    }
    left_ms = (uint16_t)(RS_V3_HOLD_START_ALL_MS - s_hold_start_all_ms);
    /* 3000..2001 → 3; 2000..1001 → 2; 1000..1 → 1 */
    return (uint8_t)((left_ms + 999u) / 1000u);
}

static uint8_t rs_v3_evt_is_fire_ctrl(uint8_t type)
{
    return (type == (uint8_t)RS_PANEL_V3_EVT_START_SP ||
            type == (uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH ||
            type == (uint8_t)RS_PANEL_V3_EVT_START_ALL_COMMIT ||
            type == (uint8_t)RS_PANEL_V3_EVT_FIRE_RESET) ? 1u : 0u;
}

static void rs_v3_evt_assign(uint8_t type, uint8_t zone, uint8_t u8_a,
                             uint16_t u16_a, uint16_t u16_b)
{
    if (s_event_seq_ctr == 0u) {
        s_event_seq_ctr = 1u;
    }
    s_event.seq = s_event_seq_ctr++;
    if (s_event_seq_ctr == 0u) {
        s_event_seq_ctr = 1u;
    }
    s_event.type = type;
    s_event.zone = zone;
    s_event.u8_a = u8_a;
    s_event.u16_a = u16_a;
    s_event.u16_b = u16_b;
}

static uint8_t rs_v3_evt_q_find(uint8_t type, uint8_t zone)
{
    uint8_t i;
    for (i = 0u; i < s_evt_q_n; i++) {
        if (s_evt_q[i].type == type && s_evt_q[i].zone == zone) {
            return i;
        }
    }
    return 0xFFu;
}

static void rs_v3_evt_q_write(uint8_t idx, uint8_t type, uint8_t zone, uint8_t u8_a,
                              uint16_t u16_a, uint16_t u16_b)
{
    s_evt_q[idx].type = type;
    s_evt_q[idx].zone = zone;
    s_evt_q[idx].u8_a = u8_a;
    s_evt_q[idx].u16_a = u16_a;
    s_evt_q[idx].u16_b = u16_b;
    s_evt_q[idx].seq = 0u;
}

/* Место в очереди: выкинуть самый старый не-fire; если все fire — голову. */
static void rs_v3_evt_q_make_space(void)
{
    uint8_t i;
    uint8_t drop;
    uint8_t have_drop = 0u;

    if (s_evt_q_n < RS_V3_EVT_Q_MAX) {
        return;
    }
    drop = 0u;
    for (i = s_evt_q_n; i > 0u; i--) {
        if (rs_v3_evt_is_fire_ctrl(s_evt_q[i - 1u].type) == 0u) {
            drop = (uint8_t)(i - 1u);
            have_drop = 1u;
            break;
        }
    }
    if (have_drop == 0u) {
        drop = 0u;
    }
    for (i = (uint8_t)(drop + 1u); i < s_evt_q_n; i++) {
        s_evt_q[i - 1u] = s_evt_q[i];
    }
    s_evt_q_n--;
}

/* Обычное событие — в хвост (дедуп type+zone). */
static uint8_t rs_v3_evt_enqueue_tail(uint8_t type, uint8_t zone, uint8_t u8_a,
                                      uint16_t u16_a, uint16_t u16_b)
{
    uint8_t i = rs_v3_evt_q_find(type, zone);
    if (i != 0xFFu) {
        rs_v3_evt_q_write(i, type, zone, u8_a, u16_a, u16_b);
        return 1u;
    }
    rs_v3_evt_q_make_space();
    rs_v3_evt_q_write(s_evt_q_n, type, zone, u8_a, u16_a, u16_b);
    s_evt_q_n++;
    return 1u;
}

/* Fire-ctrl: после последнего fire в очереди (перед обычными), сдвиг хвоста. */
static uint8_t rs_v3_evt_enqueue_priority(uint8_t type, uint8_t zone, uint8_t u8_a,
                                          uint16_t u16_a, uint16_t u16_b)
{
    uint8_t i;
    uint8_t pos;
    uint8_t found = rs_v3_evt_q_find(type, zone);

    if (found != 0xFFu) {
        /* Уже в очереди: обновить поля; если не в fire-префиксе — перенести. */
        if (found > 0u) {
            uint8_t all_fire_before = 1u;
            for (i = 0u; i < found; i++) {
                if (rs_v3_evt_is_fire_ctrl(s_evt_q[i].type) == 0u) {
                    all_fire_before = 0u;
                    break;
                }
            }
            if (all_fire_before != 0u) {
                rs_v3_evt_q_write(found, type, zone, u8_a, u16_a, u16_b);
                return 1u;
            }
            /* Убрать и вставить заново с приоритетом. */
            for (i = found + 1u; i < s_evt_q_n; i++) {
                s_evt_q[i - 1u] = s_evt_q[i];
            }
            s_evt_q_n--;
        } else {
            rs_v3_evt_q_write(0u, type, zone, u8_a, u16_a, u16_b);
            return 1u;
        }
    }

    pos = 0u;
    for (i = 0u; i < s_evt_q_n; i++) {
        if (rs_v3_evt_is_fire_ctrl(s_evt_q[i].type) != 0u) {
            pos = (uint8_t)(i + 1u);
        } else {
            break;
        }
    }

    rs_v3_evt_q_make_space();
    for (i = s_evt_q_n; i > pos; i--) {
        s_evt_q[i] = s_evt_q[i - 1u];
    }
    rs_v3_evt_q_write(pos, type, zone, u8_a, u16_a, u16_b);
    s_evt_q_n++;
    return 1u;
}

static void rs_v3_evt_flush_queue(void)
{
    uint8_t i;
    if (s_event.seq != 0u || s_evt_q_n == 0u) {
        return;
    }
    rs_v3_evt_assign(s_evt_q[0].type, s_evt_q[0].zone, s_evt_q[0].u8_a,
                     s_evt_q[0].u16_a, s_evt_q[0].u16_b);
    for (i = 1u; i < s_evt_q_n; i++) {
        s_evt_q[i - 1u] = s_evt_q[i];
    }
    s_evt_q_n--;
}

uint8_t RsPanelV3Slave_PostEvent(uint8_t type, uint8_t zone, uint8_t u8_a,
                                 uint16_t u16_a, uint16_t u16_b)
{
    const uint8_t is_fire = rs_v3_evt_is_fire_ctrl(type);

    if (type == (uint8_t)RS_PANEL_V3_EVT_NONE) {
        return 0u;
    }

    /* Слот свободен: fire сразу в полёт; обычные — в очередь и flush головы. */
    if (s_event.seq == 0u) {
        if (is_fire != 0u) {
            rs_v3_evt_assign(type, zone, u8_a, u16_a, u16_b);
            return 1u;
        }
        (void)rs_v3_evt_enqueue_tail(type, zone, u8_a, u16_a, u16_b);
        rs_v3_evt_flush_queue();
        return 1u;
    }

    /* Слот занят — не вытесняем in-flight. */
    if (is_fire != 0u) {
        return rs_v3_evt_enqueue_priority(type, zone, u8_a, u16_a, u16_b);
    }
    return rs_v3_evt_enqueue_tail(type, zone, u8_a, u16_a, u16_b);
}

static void rs_v3_evt_retry_sticky(void)
{
    if (s_start_all_evt_pending != 0u) {
        if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_START_ALL_COMMIT, 0u, 0u, 0u, 0u) != 0u) {
            s_start_all_evt_pending = 0u;
        }
    }
    if (s_fire_reset_evt_pending != 0u) {
        if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_FIRE_RESET, 0u, 0u, 0u, 0u) != 0u) {
            s_fire_reset_evt_pending = 0u;
        }
    }
}

uint8_t RsPanelV3Slave_IsEventPending(void)
{
    return (s_event.seq != 0u) ? 1u : 0u;
}

uint8_t RsPanelV3Slave_IsEventIdle(void)
{
    return (s_event.seq == 0u && s_evt_q_n == 0u) ? 1u : 0u;
}

static void rs_v3_restore_start_all_idle_leds(void)
{
    Led_SetBrightness(LED_BUT_START_ALL, LED_BUT_DIM_BRIGHTNESS);
    Led_SetBrightness(LED_STR_START_ALL, LED_BUT_DIM_BRIGHTNESS);
    Led_Set(LED_BUT_START_ALL, 0u);
    Led_Set(LED_STR_START_ALL, 1u);
}

static uint8_t rs_v3_has_attention_faults(void)
{
    uint16_t i;
    for (i = 0u; i < s_active_fault_count; i++) {
        if ((s_active_faults[i].flags & RS_PANEL_V3_FAULT_FLAG_ATTENTION) != 0u) {
            return 1u;
        }
    }
    return 0u;
}

/* signal=1 → сигнальный пакет, затем WAIT_DUTY; signal=0 → сразу дежурный. */
static void rs_v3_fault_beep(uint8_t attn, uint8_t signal)
{
    if (signal != 0u) {
        if (attn != 0u) {
            Beeper_StartPulseTrain(SOUND_ATTN_SIGNAL_ON_MS, SOUND_ATTN_SIGNAL_OFF_MS,
                                   SOUND_ATTN_SIGNAL_PULSES, 0u);
            s_fault_sound_deadline_ms = HAL_GetTick() + (uint32_t)RS_V3_ATTN_SIG_MS;
        } else {
            Beeper_StartPulseTrain(SOUND_FAULT_SIGNAL_ON_MS, SOUND_FAULT_SIGNAL_OFF_MS,
                                   SOUND_FAULT_SIGNAL_PULSES, 0u);
            s_fault_sound_deadline_ms = HAL_GetTick() + (uint32_t)RS_V3_FAULT_SIG_MS;
        }
        s_fault_sound_phase = RS_V3_FS_WAIT_DUTY;
        return;
    }
    if (attn != 0u) {
        Beeper_StartPulseTrain(SOUND_ATTN_DUTY_ON_MS, SOUND_ATTN_DUTY_OFF_MS,
                               SOUND_ATTN_DUTY_PULSES, SOUND_ATTN_DUTY_REPEAT_MS);
    } else {
        Beeper_StartPulseTrain(SOUND_FAULT_DUTY_ON_MS, SOUND_FAULT_DUTY_OFF_MS,
                               SOUND_FAULT_DUTY_PULSES, SOUND_FAULT_DUTY_REPEAT_MS);
    }
    s_fault_sound_phase = RS_V3_FS_DUTY;
    s_fault_sound_deadline_ms = 0u;
}

/* START_ALL hold / unmute — вернуть дежурный (не сигнальный). */
static void rs_v3_restore_fault_beep_if_needed(void)
{
    if (s_fire_active != 0u) {
        return;
    }
    if (s_active_fault_count == 0u) {
        s_fault_sound_phase = RS_V3_FS_IDLE;
        s_fault_sound_deadline_ms = 0u;
        return;
    }
    rs_v3_fault_beep(rs_v3_has_attention_faults(), 0u);
}

/*
 * После CLEAR_ALL→SET: SIGNAL только при реальном росте count (как на ППКУ).
 * Укорочение/тот же n — не рестартовать DUTY (иначе период «прыгает»).
 */
static void rs_v3_fault_sound_after_resync(void)
{
    const uint16_t pre = s_fault_pre_clear_count;
    s_fault_pre_clear_count = 0u;
    if (s_fire_active != 0u) {
        return;
    }
    if (s_active_fault_count == 0u) {
        Beeper_StopPattern();
        s_fault_sound_phase = RS_V3_FS_IDLE;
        s_fault_sound_deadline_ms = 0u;
        return;
    }
    if (s_active_fault_count > pre) {
        Beeper_ResumeSoundOnNewEvent();
        rs_v3_fault_beep(rs_v3_has_attention_faults(), 1u);
        /* Реальный рост после CLEAR_ALL→SET — на MAIN с меню. */
        if (MenuUi_IsMainScreenActive() == 0u || MenuUi_GetMenuSessionScreen() != 0u) {
            RsPanelEndpoint_QueueGotoMain();
        }
        return;
    }
    if (s_fault_sound_phase == RS_V3_FS_IDLE) {
        rs_v3_fault_beep(rs_v3_has_attention_faults(), 0u);
    }
}

static uint8_t rs_v3_zone_is_terminal(uint8_t st)
{
    return (st == (uint8_t)RS_PANEL_V3_ZONE_EXT_DONE ||
            st == (uint8_t)RS_PANEL_V3_ZONE_EXT_FAIL ||
            st == (uint8_t)RS_PANEL_V3_ZONE_EXT_ABORT) ? 1u : 0u;
}

static uint8_t rs_v3_zones_want_fire1(void)
{
    uint8_t i;
    uint8_t any = 0u;
    for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
        const uint8_t st = s_zones.items[i].status;
        if (st == 0u) {
            continue;
        }
        any = 1u;
        if (st != (uint8_t)RS_PANEL_V3_ZONE_FIRE1) {
            return 0u;
        }
    }
    return any;
}

/* Как на ППКУ: ALERT → START(тушение) → DUTY(ТУШ.ВЫП); LED_START при тушении. */
static void rs_v3_sync_fire_indication(void)
{
    uint8_t i;
    uint8_t any_ext = 0u;
    uint8_t any_zone = 0u;
    uint8_t all_terminal = 1u;
    uint8_t desired;
    uint8_t fire1;

    if (s_hold_start_all != 0u && s_hold_start_all_committed == 0u) {
        return;
    }

    if (s_fire_active == 0u) {
        if (s_fire_snd_phase != RS_V3_FIRE_SND_IDLE) {
            Beeper_ContinuousOff();
            Beeper_FireAlarmOff();
            Beeper_StopPattern();
            Led_Set(LED_START, 0u);
            s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
            rs_v3_restore_fault_beep_if_needed();
        }
        return;
    }

    for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
        const uint8_t st = s_zones.items[i].status;
        if (st == 0u) {
            continue;
        }
        any_zone = 1u;
        if (st == (uint8_t)RS_PANEL_V3_ZONE_EXTINGUISHING) {
            any_ext = 1u;
        }
        if (rs_v3_zone_is_terminal(st) == 0u) {
            all_terminal = 0u;
        }
    }

    if (any_ext != 0u) {
        desired = RS_V3_FIRE_SND_START;
    } else if (any_zone != 0u && all_terminal != 0u) {
        desired = RS_V3_FIRE_SND_DUTY;
    } else {
        desired = RS_V3_FIRE_SND_ALERT;
    }

    if (desired == s_fire_snd_phase) {
        Led_Set(LED_START, (desired == RS_V3_FIRE_SND_START) ? 1u : 0u);
        return;
    }

    fire1 = rs_v3_zones_want_fire1();
    s_fault_sound_phase = RS_V3_FS_IDLE;
    s_fault_sound_deadline_ms = 0u;
    Beeper_ContinuousOff();
    Beeper_FireAlarmOff();
    Beeper_StopPattern();

    if (desired == RS_V3_FIRE_SND_ALERT) {
        Beeper_ResumeSoundOnNewEvent();
        if (fire1 != 0u) {
            Beeper_StartPulseTrain(SOUND_FIRE1_SIGNAL_ON_MS, SOUND_FIRE1_SIGNAL_OFF_MS,
                                   SOUND_FIRE1_SIGNAL_PULSES, SOUND_FIRE1_SIGNAL_REPEAT_MS);
        } else {
            Beeper_FireAlarmOn();
        }
        Led_Set(LED_START, 0u);
    } else if (desired == RS_V3_FIRE_SND_START) {
        Beeper_ResumeSoundOnNewEvent();
        Beeper_StartPulseTrain(BEEPER_PATTERN_START_ON_MS, BEEPER_PATTERN_START_OFF_MS,
                               BEEPER_PATTERN_START_PULSES, BEEPER_PATTERN_START_REPEAT_MS);
        Led_Set(LED_START, 1u);
        Led_SetBrightness(LED_START, LED_STATUS_MAX_BRIGHTNESS);
        Led_ForceStatusBright(LED_START);
    } else {
        /* DUTY: не Resume — иначе после ТУШ.ВЫП снова «орёт» unmute. */
        if (fire1 != 0u) {
            Beeper_StartPulseTrain(SOUND_FIRE1_DUTY_ON_MS, SOUND_FIRE1_DUTY_OFF_MS,
                                   SOUND_FIRE1_DUTY_PULSES, SOUND_FIRE1_DUTY_REPEAT_MS);
        } else {
            Beeper_StartPulseTrain(BEEPER_PATTERN_FIRE_ON_MS, BEEPER_PATTERN_FIRE_OFF_MS,
                                   BEEPER_PATTERN_FIRE_PULSES, BEEPER_PATTERN_FIRE_REPEAT_MS);
        }
        Led_Set(LED_START, 0u);
    }
    s_fire_snd_phase = desired;
}

void RsPanelV3Slave_OnSoundEnabled(void)
{
    if (s_fire_active != 0u) {
        /* Сброс фазы — переиграть актуальный профиль после unmute. */
        s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
        rs_v3_sync_fire_indication();
    } else {
        rs_v3_restore_fault_beep_if_needed();
    }
}

static void rs_v3_recalc_catalog_crc(void)
{
    uint8_t z;
    s_zones_crc = 0xFFFFu;
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (s_zone_name_valid[z] == 0u) {
            continue;
        }
        s_zones_crc = RsPanelV3_ZoneCatalogCrc_Add(s_zones_crc, z, s_zone_name_by_id[z]);
    }
    s_devices_crc = RsPanelV3_DeviceCatalogCrc(s_device_catalog, s_device_catalog_count);
    s_faults_crc = RsPanelV3_FaultEvtCrc(s_active_faults, s_active_fault_count);
}

static void rs_v3_sync_host_cache_from_catalog(void)
{
    PanelHostCache *host = PanelHostCache_Get();
    uint8_t z;
    uint16_t i;
    uint8_t slot = 0u;

    memset(host->zone_name, 0, sizeof(host->zone_name));
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (s_zone_name_valid[z] == 0u) {
            continue;
        }
        {
            size_t n = strlen(s_zone_name_by_id[z]);
            if (n > ZONE_NAME_SIZE) {
                n = ZONE_NAME_SIZE;
            }
            memcpy(host->zone_name[z - 1u], s_zone_name_by_id[z], n);
        }
    }

    memset(host->devices, 0, sizeof(host->devices));
    for (i = 0u; i < s_device_catalog_count && slot < MAX_MCU_IN_BUS; i++) {
        const RsPanelV3DeviceItem *d = &s_device_catalog[i];
        host->devices[slot].zone = d->zone;
        host->devices[slot].l_adr = d->l_adr;
        host->devices[slot].h_adr = d->h_adr;
        host->devices[slot].d_type = d->d_type;
        host->devices[slot].uid0 = d->uid0;
        host->devices[slot].uid1 = d->uid1;
        host->devices[slot].uid2 = d->uid2;
        slot++;
    }
}

static void rs_v3_catalog_mark_flash_dirty(void)
{
    s_catalog_flash_dirty = 1u;
    s_catalog_flash_dirty_ms = HAL_GetTick();
}

static void rs_v3_catalog_flash_save_if_dirty(void)
{
    PanelCatalogBlob blob;
    uint8_t z;
    uint16_t i;

    if (s_catalog_flash_dirty == 0u) {
        return;
    }
    memset(&blob, 0, sizeof(blob));
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (s_zone_name_valid[z] == 0u) {
            continue;
        }
        blob.zone_valid[z - 1u] = 1u;
        memcpy(blob.zone_name[z - 1u], s_zone_name_by_id[z], RS_PANEL_V3_ZONE_NAME_LEN);
    }
    blob.device_count = s_device_catalog_count;
    for (i = 0u; i < s_device_catalog_count && i < RS_PANEL_V3_MAX_CATALOG_MCU; i++) {
        blob.devices[i] = s_device_catalog[i];
        blob.devices[i].op = (uint8_t)RS_PANEL_V3_STREAM_OP_ADD;
    }
    blob.zones_crc = s_zones_crc;
    blob.devices_crc = s_devices_crc;
    PanelCatalog_FlashWrite(&blob);
    s_catalog_flash_dirty = 0u;
}

static uint8_t rs_v3_catalog_flash_load(void)
{
    PanelCatalogBlob blob;
    uint8_t z;
    uint16_t i;

    if (PanelCatalog_FlashRead(&blob) == 0u) {
        return 0u;
    }
    memset(s_zone_name_by_id, 0, sizeof(s_zone_name_by_id));
    memset(s_zone_name_valid, 0, sizeof(s_zone_name_valid));
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (blob.zone_valid[z - 1u] == 0u) {
            continue;
        }
        memcpy(s_zone_name_by_id[z], blob.zone_name[z - 1u], RS_PANEL_V3_ZONE_NAME_LEN);
        s_zone_name_by_id[z][RS_PANEL_V3_ZONE_NAME_LEN - 1u] = '\0';
        s_zone_name_valid[z] = 1u;
    }
    s_device_catalog_count = blob.device_count;
    if (s_device_catalog_count > RS_PANEL_V3_MAX_CATALOG_MCU) {
        s_device_catalog_count = RS_PANEL_V3_MAX_CATALOG_MCU;
    }
    memset(s_device_catalog, 0, sizeof(s_device_catalog));
    for (i = 0u; i < s_device_catalog_count; i++) {
        s_device_catalog[i] = blob.devices[i];
    }
    rs_v3_recalc_catalog_crc();
    /* CRC в blob — контроль целостности; при расхождении считаем Flash битым. */
    if (blob.zones_crc != s_zones_crc || blob.devices_crc != s_devices_crc) {
        memset(s_zone_name_by_id, 0, sizeof(s_zone_name_by_id));
        memset(s_zone_name_valid, 0, sizeof(s_zone_name_valid));
        memset(s_device_catalog, 0, sizeof(s_device_catalog));
        s_device_catalog_count = 0u;
        rs_v3_recalc_catalog_crc();
        return 0u;
    }
    rs_v3_sync_host_cache_from_catalog();
    s_catalog_ready = 1u;
    s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
    return 1u;
}

static const RsPanelV3DeviceItem *rs_v3_find_mcu(const RsPanelV3McuKey *key)
{
    uint16_t i;
    if (key == 0) {
        return 0;
    }
    for (i = 0u; i < s_device_catalog_count; i++) {
        const RsPanelV3DeviceItem *d = &s_device_catalog[i];
        if (d->h_adr == key->h_adr && d->l_adr == key->l_adr &&
            d->uid0 == key->uid0 && d->uid1 == key->uid1 && d->uid2 == key->uid2) {
            return d;
        }
    }
    for (i = 0u; i < s_device_catalog_count; i++) {
        const RsPanelV3DeviceItem *d = &s_device_catalog[i];
        if (d->h_adr == key->h_adr) {
            return d;
        }
    }
    return 0;
}

static void rs_v3_zone_name(uint8_t zone, char *out, uint16_t out_sz)
{
    if (out == 0 || out_sz == 0u) {
        return;
    }
    out[0] = '\0';
    if (zone > 0u && zone <= 32 && s_zone_name_valid[zone] != 0u) {
        strncpy(out, s_zone_name_by_id[zone], out_sz - 1u);
        out[out_sz - 1u] = '\0';
        return;
    }
    /* Fallback: имена из меню блокировки зон (host cache), как на ППКУ1 из PPKYConfig. */
    if (zone > 0u && zone <= 32) {
        const PanelHostCache *host = PanelHostCache_GetConst();
        const int8_t *hn = host->zone_name[zone - 1u];
        if (hn[0] != 0) {
            size_t i;
            for (i = 0u; i + 1u < out_sz && i < (size_t)ZONE_NAME_SIZE && hn[i] != 0; i++) {
                out[i] = (char)hn[i];
            }
            out[i] = '\0';
            while (i > 0u && out[i - 1u] == ' ') {
                out[--i] = '\0';
            }
            if (out[0] != '\0') {
                return;
            }
        }
    }
    (void)snprintf(out, out_sz, "ЗОНА %u", (unsigned)zone);
}

static const char *rs_v3_mcu_type_name(uint8_t d_type)
{
    switch (d_type) {
    case DEVICE_MCU_IGN_TYPE: return "MKU IGN";
    case DEVICE_MCU_TC_TYPE:  return "MKU TC";
    case DEVICE_MCU_K1:       return "MKU K1";
    case DEVICE_MCU_K2:       return "MKU K2";
    case DEVICE_MCU_K3:       return "MKU K3";
    case DEVICE_MCU_KR:       return "MKU KR";
    default:                  return "MKU";
    }
}

static const char *rs_v3_ch_type_short(uint8_t v_d_type)
{
    switch (v_d_type) {
    case DEVICE_DPT_TYPE: return "ДПТ";
    case DEVICE_IGNITER_TYPE: return "СП";
    case DEVICE_BUTTON_TYPE: return "КН";
    case DEVICE_LSWITCH_TYPE: return "КОН";
    default: return "???";
    }
}

static void rs_v3_format_ppku_sn(const RsPanelV3FaultEvtItem *fe, char *out, uint16_t out_sz)
{
    const PanelHostCache *host = PanelHostCache_GetConst();
    uint32_t u0 = host->host_uid0;
    uint32_t u1 = host->host_uid1;
    uint32_t u2 = host->host_uid2;
    if (out == 0 || out_sz == 0u) {
        return;
    }
    /* UID с хоста в FAULT_EVT.mcu (кэш host_uid* на панели обычно пуст). */
    if (fe != 0 && (fe->flags & RS_PANEL_V3_FAULT_FLAG_HAS_MCU) != 0u &&
        (fe->mcu.uid0 != 0u || fe->mcu.uid1 != 0u || fe->mcu.uid2 != 0u)) {
        u0 = fe->mcu.uid0;
        u1 = fe->mcu.uid1;
        u2 = fe->mcu.uid2;
    }
    (void)snprintf(out, out_sz, "ППКУ S/N:%08lX:%08lX:%08lX",
                   (unsigned long)u0, (unsigned long)u1, (unsigned long)u2);
}

/* Как ППКУ1 Warning_FormatMkuAndSerial: низ бегущей = «имя зоны + МКУ + h_adr + S/N». */
static uint8_t rs_v3_fault_zone_id(const RsPanelV3FaultEvtItem *fe,
                                   const RsPanelV3DeviceItem *mcu)
{
    if (mcu != 0 && mcu->zone != 0u) {
        return mcu->zone;
    }
    if (fe != 0 && (fe->flags & RS_PANEL_V3_FAULT_FLAG_HAS_ZONE) != 0u &&
        fe->zone != 0u) {
        return fe->zone;
    }
    return 0u;
}

static void rs_v3_format_mku_detail(const RsPanelV3DeviceItem *mcu, char *out, uint16_t out_sz)
{
    char zone_name[RS_PANEL_V3_ZONE_NAME_LEN];
    if (out == 0 || out_sz == 0u) {
        return;
    }
    if (mcu == 0) {
        (void)snprintf(out, out_sz, "МКУ ?");
        return;
    }
    rs_v3_zone_name(mcu->zone, zone_name, sizeof(zone_name));
    (void)snprintf(out, out_sz, "%s %s %u S/N:%08lX:%08lX:%08lX",
                   zone_name, rs_v3_mcu_type_name(mcu->d_type), (unsigned)mcu->h_adr,
                   (unsigned long)mcu->uid0, (unsigned long)mcu->uid1,
                   (unsigned long)mcu->uid2);
}

static void rs_v3_format_mku_detail_key(const RsPanelV3FaultEvtItem *fe,
                                        const RsPanelV3McuKey *key,
                                        char *out, uint16_t out_sz)
{
    char zone_name[RS_PANEL_V3_ZONE_NAME_LEN];
    uint8_t zone;
    if (out == 0 || out_sz == 0u) {
        return;
    }
    if (key == 0) {
        (void)snprintf(out, out_sz, "МКУ ?");
        return;
    }
    zone = rs_v3_fault_zone_id(fe, 0);
    if (zone != 0u) {
        /* Без записи в каталоге МКУ — всё равно имя зоны (как ППКУ1). */
        rs_v3_zone_name(zone, zone_name, sizeof(zone_name));
        (void)snprintf(out, out_sz, "%s МКУ %u S/N:%08lX:%08lX:%08lX",
                       zone_name, (unsigned)key->h_adr,
                       (unsigned long)key->uid0, (unsigned long)key->uid1,
                       (unsigned long)key->uid2);
        return;
    }
    (void)snprintf(out, out_sz, "МКУ %u S/N:%08lX:%08lX:%08lX",
                   (unsigned)key->h_adr,
                   (unsigned long)key->uid0, (unsigned long)key->uid1,
                   (unsigned long)key->uid2);
}

static void rs_v3_format_mku_fault_detail(const RsPanelV3FaultEvtItem *fe,
                                          const RsPanelV3DeviceItem *mcu,
                                          char *out, uint16_t out_sz)
{
    if (mcu != 0) {
        rs_v3_format_mku_detail(mcu, out, out_sz);
        return;
    }
    if (fe != 0 && (fe->flags & RS_PANEL_V3_FAULT_FLAG_HAS_MCU) != 0u) {
        rs_v3_format_mku_detail_key(fe, &fe->mcu, out, out_sz);
        return;
    }
    {
        uint8_t zone = rs_v3_fault_zone_id(fe, 0);
        if (zone != 0u) {
            rs_v3_zone_name(zone, out, out_sz);
            return;
        }
    }
    (void)snprintf(out, out_sz, "МКУ ?");
}

static void rs_v3_format_fault_line(const RsPanelV3FaultEvtItem *fe,
                                    char *title, uint16_t title_sz,
                                    char *detail, uint16_t detail_sz)
{
    const RsPanelV3DeviceItem *mcu = 0;
    if (fe == 0 || title == 0 || detail == 0) {
        return;
    }
    title[0] = '\0';
    detail[0] = '\0';
    if ((fe->flags & RS_PANEL_V3_FAULT_FLAG_HAS_MCU) != 0u) {
        mcu = rs_v3_find_mcu(&fe->mcu);
    }

    switch (fe->code) {
    case RS_PANEL_V3_FAULT_ATTN_LSWITCH:
        (void)snprintf(title, title_sz, "\x01ОТКРЫТИЕ");
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_ATTN_DPT:
        (void)snprintf(title, title_sz, "\x01ТЕМП. %d", (int)fe->extra);
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_PPKU_POWER_OUT1:
    case RS_PANEL_V3_FAULT_PPKU_POWER_OUT2:
        (void)snprintf(title, title_sz, "ВЫХОД %u",
                       (unsigned)(fe->code - RS_PANEL_V3_FAULT_PPKU_POWER_OUT1 + 1u));
        rs_v3_format_ppku_sn(fe, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_PPKU_POWER_IN1:
    case RS_PANEL_V3_FAULT_PPKU_POWER_IN2:
        (void)snprintf(title, title_sz, "ПИТАНИЕ %u",
                       (unsigned)(fe->code - RS_PANEL_V3_FAULT_PPKU_POWER_IN1 + 1u));
        rs_v3_format_ppku_sn(fe, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_LINE_BREAK:
    case RS_PANEL_V3_FAULT_LINE_SHORT:
    case RS_PANEL_V3_FAULT_LINE_OTHER: {
        const char *fault = "ОБРЫВ";
        if (fe->code == RS_PANEL_V3_FAULT_LINE_SHORT) {
            fault = "КЗ";
        } else if (fe->code == RS_PANEL_V3_FAULT_LINE_OTHER) {
            fault = "НЕИСП";
        }
        (void)snprintf(title, title_sz, "%s %s%u", fault,
                       rs_v3_ch_type_short(fe->ch_type), (unsigned)fe->ch_l_adr);
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    }
    case RS_PANEL_V3_FAULT_MCU_CAN_BREAK:
    case RS_PANEL_V3_FAULT_MCU_CAN_SHORT:
        (void)snprintf(title, title_sz, "%s CAN%u",
                       (fe->code == RS_PANEL_V3_FAULT_MCU_CAN_SHORT) ? "КЗ" : "ОБРЫВ",
                       (unsigned)fe->can_idx);
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_PPKU_CAN_BREAK:
    case RS_PANEL_V3_FAULT_PPKU_CAN_SHORT:
        (void)snprintf(title, title_sz, "%s CAN%u",
                       (fe->code == RS_PANEL_V3_FAULT_PPKU_CAN_SHORT) ? "КЗ" : "ОБРЫВ",
                       (unsigned)fe->can_idx);
        rs_v3_format_ppku_sn(fe, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_MCU_POSITION:
        (void)snprintf(title, title_sz, "ПОЗИЦИЯ");
        /* ППКУ1: только «МКУ N»; если есть зона в EVT/каталоге — как остальные МКУ-fault. */
        if (rs_v3_fault_zone_id(fe, mcu) != 0u || mcu != 0) {
            rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        } else {
            (void)snprintf(detail, detail_sz, "МКУ %u",
                           (unsigned)((mcu != 0) ? mcu->h_adr : fe->mcu.h_adr));
        }
        break;
    case RS_PANEL_V3_FAULT_PANEL_JOURNAL:
        (void)snprintf(title, title_sz, "ЖУРНАЛ RS");
        (void)snprintf(detail, detail_sz, "ПАНЕЛЬ %u", (unsigned)fe->panel_addr);
        break;
    case RS_PANEL_V3_FAULT_DEVICE_MISSING:
        (void)snprintf(title, title_sz, "ОТСУСТВ.");
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_DEVICE_FOUND:
        (void)snprintf(title, title_sz, "НОВОЕ");
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    case RS_PANEL_V3_FAULT_CONFIG_MISMATCH:
        (void)snprintf(title, title_sz, "ОШ. КОНФ.");
        rs_v3_format_mku_fault_detail(fe, mcu, detail, detail_sz);
        break;
    default:
        (void)snprintf(title, title_sz, "НЕИСП");
        break;
    }
}

static void rs_v3_push_faults_ui(void)
{
    char titles[16][24];
    char details[16][ZONE_NAME_SIZE + 1];
    uint8_t n;
    uint8_t i;
    /* Локальная «НЕТ СВЯЗИ» важнее списка с хоста. */
    if (PanelLinkMonitor_IsLost() != 0u) {
        return;
    }
    /* До SYS_READY на OLED только «ПРОВЕРКА» — не слать WARN (иначе АВАРИЯ+ПРОВЕРКА
     * при первом совместном старте ППКУ+панель, пока READY через ~20 с). */
    if (s_sys_ready == 0u) {
        s_fault_ui_dirty = 1u;
        s_fault_ui_dirty_ms = HAL_GetTick();
        return;
    }
    n = (s_active_fault_count > 16u) ? 16u : (uint8_t)s_active_fault_count;
    memset(titles, 0, sizeof(titles));
    memset(details, 0, sizeof(details));
    for (i = 0u; i < n; i++) {
        rs_v3_format_fault_line(&s_active_faults[i], titles[i], 24u,
                                details[i], (uint16_t)(ZONE_NAME_SIZE + 1u));
    }
    s_fault_placeholder_active = 0u;
    s_fault_ui_dirty = 0u;
    s_fault_ui_pushed_count = n;
    /* Не в UART/OnPoll: иначе TouchGFX early-return оставляет центр «ПРОВЕРКА». */
    RsPanelEndpoint_QueueWarningUi((n > 0u) ? 1u : 0u, n, titles, details);
}

static void rs_v3_mark_fault_ui_dirty(void)
{
    s_fault_ui_dirty = 1u;
    s_fault_ui_dirty_ms = HAL_GetTick();
}

/*
 * SYS_HAS_FAULTS без FAULT_SET: не показывать фейковый «НЕИСП» и не трогать
 * уже отрисованный список — ждём SET (dirty). Иначе АВАРИЯ НЕИСП на ~20 с
 * пока идёт каталог, и «залипание» старого текста при CLEAR без SET.
 */
static void rs_v3_wait_fault_evt_ui(void)
{
    if (s_fault_sound_resync != 0u) {
        return;
    }
    rs_v3_mark_fault_ui_dirty();
}

static void rs_v3_apply_zone_names(const RsPanelV3ZoneNameItem *zn)
{
    if (zn == 0) {
        return;
    }
    if (zn->op == (uint8_t)RS_PANEL_V3_STREAM_OP_CLEAR) {
        memset(s_zone_name_by_id, 0, sizeof(s_zone_name_by_id));
        memset(s_zone_name_valid, 0, sizeof(s_zone_name_valid));
        /* Не ставить NEED_CATALOG: идёт частичный rewrite; CRC ведёт master. */
        s_catalog_ready = 0u;
        rs_v3_recalc_catalog_crc();
        rs_v3_catalog_mark_flash_dirty();
        return;
    }
    if (zn->zone == 0u || zn->zone > 32) {
        return;
    }
    strncpy(s_zone_name_by_id[zn->zone], zn->name, RS_PANEL_V3_ZONE_NAME_LEN - 1u);
    s_zone_name_by_id[zn->zone][RS_PANEL_V3_ZONE_NAME_LEN - 1u] = '\0';
    s_zone_name_valid[zn->zone] = 1u;
    rs_v3_recalc_catalog_crc();
    rs_v3_sync_host_cache_from_catalog();
    rs_v3_catalog_mark_flash_dirty();
    /* Имя зоны могло прийти после FAULT_SET — пересобрать нижнюю строку. */
    if (s_active_fault_count > 0u) {
        rs_v3_mark_fault_ui_dirty();
    }
}

static void rs_v3_apply_devices(const RsPanelV3DeviceItem *dev)
{
    if (dev == 0) {
        return;
    }
    if (dev->op == (uint8_t)RS_PANEL_V3_STREAM_OP_CLEAR) {
        memset(s_device_catalog, 0, sizeof(s_device_catalog));
        s_device_catalog_count = 0u;
        s_catalog_ready = 0u;
        rs_v3_recalc_catalog_crc();
        rs_v3_catalog_mark_flash_dirty();
        return;
    }
    if (s_device_catalog_count < RS_PANEL_V3_MAX_CATALOG_MCU) {
        s_device_catalog[s_device_catalog_count] = *dev;
        s_device_catalog_count++;
        rs_v3_recalc_catalog_crc();
        rs_v3_sync_host_cache_from_catalog();
        rs_v3_catalog_mark_flash_dirty();
        /* Каталог МКУ даёт zone для detail — обновить бегущую при активных fault. */
        if (s_active_fault_count > 0u) {
            rs_v3_mark_fault_ui_dirty();
        }
    }
}

static void rs_v3_apply_fault_evt(const RsPanelV3FaultEvtItem *fe)
{
    uint16_t i;
    if (fe == 0) {
        return;
    }
    if (fe->op == (uint8_t)RS_PANEL_V3_FAULT_EVT_CLEAR_ALL) {
        /* Count до обнуления — для SIGNAL только при реальном росте после SET. */
        s_fault_pre_clear_count = s_active_fault_count;
        s_active_fault_count = 0u;
        memset(s_active_faults, 0, sizeof(s_active_faults));
        /* Список пуст — LED_ERR гасим сразу (sync тоже смотрит на count, не HAS_FAULTS). */
        Led_Set(LED_ERR, 0u);
        /* Конец фазы faults / каталога: READY, CRC зон/МКУ уже актуальны. */
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        rs_v3_recalc_catalog_crc();
        rs_v3_sync_host_cache_from_catalog();
        /* Не пушить UI сразу: держим прежний экран до пакета SET (quiet-timer).
         * Иначе АВАРИЯ n/n скачет 0→placeholder→1→2… */
        if (s_has_faults == 0u) {
            rs_v3_push_faults_ui();
            Beeper_StopPattern();
            s_fault_sound_phase = RS_V3_FS_IDLE;
            s_fault_sound_deadline_ms = 0u;
            s_fault_sound_resync = 0u;
            s_fault_pre_clear_count = 0u;
        } else {
            /* Resync: звук не трогаем на каждом SET — решим после quiet. */
            s_fault_sound_resync = 1u;
            s_fault_resync_wait_ms = HAL_GetTick();
            rs_v3_mark_fault_ui_dirty();
        }
        return;
    }
    if (fe->op == (uint8_t)RS_PANEL_V3_FAULT_EVT_SET) {
        uint8_t new_attn;
        /* Каталог без CLEAR_ALL — READY по любому SET (в т.ч. дубликату). */
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        for (i = 0u; i < s_active_fault_count; i++) {
            if (RsPanelV3_FaultEvtEqual(&s_active_faults[i], fe) != 0u) {
                rs_v3_recalc_catalog_crc();
                /* После CLEAR_ALL список короче UI — даже дубликат SET
                 * должен дать push по quiet (count уже актуален). */
                if (s_fault_sound_resync != 0u ||
                    s_active_fault_count != s_fault_ui_pushed_count) {
                    rs_v3_mark_fault_ui_dirty();
                }
                return;
            }
        }
        if (s_active_fault_count < RS_PANEL_V3_MAX_FAULTS) {
            if (s_fault_sound_resync != 0u) {
                /* Resync CLEAR→SET: порядок с хоста (newest-first), append. */
                s_active_faults[s_active_fault_count] = *fe;
                s_active_fault_count++;
            } else {
                /* Живой SET (рост без CLEAR): вставить в начало — newest-first. */
                uint16_t j;
                for (j = s_active_fault_count; j > 0u; j--) {
                    s_active_faults[j] = s_active_faults[j - 1u];
                }
                s_active_faults[0] = *fe;
                s_active_fault_count++;
            }
            new_attn = ((fe->flags & RS_PANEL_V3_FAULT_FLAG_ATTENTION) != 0u) ? 1u : 0u;
            Led_Set(LED_ERR, 1u);
            Led_ForceStatusBright(LED_ERR);
            /* Во время resync не звук/GotoMain — иначе выкидывает из меню
             * на каждый повторный SET старых неисправностей. */
            if (s_fire_active == 0u && s_fault_sound_resync == 0u) {
                Beeper_ResumeSoundOnNewEvent();
                if (new_attn != 0u || rs_v3_has_attention_faults() == 0u) {
                    /* Новое ВНИМАНИЕ / неиспр. без ВНИМАНИЯ → SIGNAL (как ППКУ). */
                    rs_v3_fault_beep(new_attn, 1u);
                }
                if (MenuUi_IsMainScreenActive() == 0u ||
                    MenuUi_GetMenuSessionScreen() != 0u) {
                    RsPanelEndpoint_QueueGotoMain();
                }
            }
        }
    } else if (fe->op == (uint8_t)RS_PANEL_V3_FAULT_EVT_CLEAR) {
        for (i = 0u; i < s_active_fault_count; i++) {
            if (RsPanelV3_FaultEvtEqual(&s_active_faults[i], fe) != 0u) {
                uint16_t j;
                for (j = i; (uint16_t)(j + 1u) < s_active_fault_count; j++) {
                    s_active_faults[j] = s_active_faults[j + 1u];
                }
                s_active_fault_count--;
                break;
            }
        }
        if (s_active_fault_count == 0u) {
            Led_Set(LED_ERR, 0u);
            Beeper_StopPattern();
            s_fault_sound_phase = RS_V3_FS_IDLE;
            s_fault_sound_deadline_ms = 0u;
            s_fault_sound_resync = 0u;
        } else if (s_fault_sound_phase == RS_V3_FS_DUTY) {
            rs_v3_fault_beep(rs_v3_has_attention_faults(), 0u);
        }
    }
    rs_v3_recalc_catalog_crc();
    rs_v3_mark_fault_ui_dirty();
}

static void rs_v3_apply_zones(const RsPanelV3Zones *z)
{
    uint8_t i;
    if (z == 0) {
        return;
    }
    s_zones = *z;
    s_zones_valid = 1u;
    for (i = 0u; i < z->count && i < RS_PANEL_V3_MAX_ZONES; i++) {
        if (z->items[i].status == (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN ||
            z->items[i].status == (uint8_t)RS_PANEL_V3_ZONE_PAUSED) {
            s_local_timer_s[i] = z->items[i].remaining_s;
        }
    }
    /* Push into existing UI bridge as MAIN_FIRE-like snapshot. */
    {
        char names[16][ZONE_NAME_SIZE + 1];
        uint8_t modes[16];
        uint8_t rems[16];
        uint8_t n = z->count;
        uint8_t active = (n > 0u) ? 1u : 0u;
        uint8_t mode = 0u;
        uint8_t rem = 0u;
        if (n > 16u) {
            n = 16u;
        }
        memset(names, 0, sizeof(names));
        for (i = 0u; i < n; i++) {
            if (z->items[i].name[0] != '\0') {
                strncpy(names[i], z->items[i].name, ZONE_NAME_SIZE);
                names[i][ZONE_NAME_SIZE] = '\0';
            } else {
                /* Как ППКУ1: бегущая — имя зоны (каталог ZONE_NAMES), не ППКУ S/N. */
                rs_v3_zone_name(z->items[i].zone, names[i], (uint16_t)sizeof(names[i]));
            }
            modes[i] = z->items[i].status;
            rems[i] = s_local_timer_s[i];
            if (i == 0u) {
                mode = modes[i];
                rem = rems[i];
            }
        }
        PanelUiBridge_SetFireStatus(active, mode, rem, n, names, modes, rems);
    }
    rs_v3_sync_fire_indication();
}

uint8_t RsPanelV3Slave_OnPoll(const uint8_t *payload, uint16_t len)
{
    RsPanelV3Poll poll;
    if (payload == 0 || len < 1u || payload[0] != RS_PANEL_V3_VERSION) {
        return 0u;
    }
    if (RsPanelV3_DecodePoll(payload, len, &poll) == 0u) {
        return 0u;
    }
    if (s_v3_active == 0u) {
        s_v3_active = 1u;
        /* После Led_Init / перехода в v3 — снова дежурная надпись ПУСК ОБЩИЙ. */
        if (s_hold_start_all == 0u) {
            rs_v3_restore_start_all_idle_leds();
        }
    }

    if (poll.ack.result != (uint8_t)RS_PANEL_V3_ACK_NONE &&
        poll.ack.seq != 0u && poll.ack.seq == s_event.seq) {
        s_event.seq = 0u;
        s_event.type = (uint8_t)RS_PANEL_V3_EVT_NONE;
        rs_v3_evt_retry_sticky();
        rs_v3_evt_flush_queue();
    }

    {
        const uint8_t prev_ready = s_sys_ready;
        const uint8_t prev_cfg = s_config_active;
        const uint8_t prev_fire = s_fire_active;
        /* addr_scan probe: только SYS с flags=0, без TIME/каталога — не сбрасывать
         * SYS_READY (иначе «ПРОВЕРКА» навечно, пока мастер не сделал ADD). */
        const uint8_t scan_probe =
            (poll.sys.flags == 0u &&
             poll.has_time == 0u &&
             poll.has_zones == 0u &&
             poll.has_zone_names == 0u &&
             poll.has_devices == 0u &&
             poll.has_fault_evt == 0u &&
             poll.has_fault_op == 0u &&
             poll.has_config == 0u &&
             poll.has_event_reply == 0u) ? 1u : 0u;

        if (scan_probe == 0u) {
            s_sys_ready = ((poll.sys.flags & RS_PANEL_V3_SYS_READY) != 0u) ? 1u : 0u;
            s_fire_active =
                ((poll.sys.flags & RS_PANEL_V3_SYS_FIRE_ACTIVE) != 0u) ? 1u : 0u;
            s_config_active =
                ((poll.sys.flags & RS_PANEL_V3_SYS_CONFIG_ACTIVE) != 0u) ? 1u : 0u;
            s_has_faults =
                ((poll.sys.flags & RS_PANEL_V3_SYS_HAS_FAULTS) != 0u) ? 1u : 0u;
            s_power_input_fault =
                ((poll.sys.flags & RS_PANEL_V3_SYS_POWER_INPUT_FAULT) != 0u) ? 1u : 0u;

            if (prev_fire != s_fire_active) {
                if (s_fire_active != 0u) {
                    s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
                }
                rs_v3_sync_fire_indication();
            }

            /* ППКУ — источник истины для mute / beep_block (в т.ч. откат при ACK_DENIED). */
            {
                const uint8_t prev_beep = PanelHostCache_GetConst()->beep;
                const uint8_t sound_on =
                    ((poll.sys.flags & RS_PANEL_V3_SYS_SOUND_ON) != 0u) ? 1u : 0u;
                const uint8_t sound_blk =
                    ((poll.sys.flags & RS_PANEL_V3_SYS_SOUND_BLOCKED) != 0u) ? 1u : 0u;
                PanelHostCache_ApplySoundState(sound_on, sound_blk);
                if (prev_beep == 0u && sound_on != 0u) {
                    if (s_fire_active != 0u) {
                        s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
                        rs_v3_sync_fire_indication();
                    } else {
                        rs_v3_restore_fault_beep_if_needed();
                    }
                }
            }

            /* Значок WiFi: POLL SYS. Пока WIFI_SET не подтверждён — не затирать
             * локальный «Вкл» кадром, собранным до обработки EVT. */
            if (s_event.seq == 0u || s_event.type != (uint8_t)RS_PANEL_V3_EVT_WIFI_SET) {
                const uint8_t wifi_on =
                    ((poll.sys.flags & RS_PANEL_V3_SYS_WIFI_ON) != 0u) ? 1u : 0u;
                const uint8_t wifi_link =
                    ((poll.sys.flags & RS_PANEL_V3_SYS_WIFI_LINK) != 0u) ? 1u : 0u;
                const uint8_t session = (uint8_t)(wifi_on != 0u || wifi_link != 0u);
                PanelConnectionCache_SetRemoteStatus(wifi_on,
                                                     PanelHostCache_GetConst()->rs485_on);
                PanelEspManager_SetRemoteStatus(session, wifi_link, wifi_link, session);
            }

            if (prev_ready == 0u && s_sys_ready != 0u) {
                /* В UART только очередь: WARN → SYS_READY в ProcessDeferredUi (TouchGFX).
                 * Прямой Notify/SetWarning из OnPoll снимал флаги «ПРОВЕРКА», а центр
                 * не перерисовывался до UP/DOWN или меню. */
                MenuUi_SetMainScreenActive(1u);
                s_fault_ui_dirty = 0u;
                if (s_active_fault_count > 0u) {
                    rs_v3_push_faults_ui();
                } else if (s_has_faults != 0u) {
                    rs_v3_wait_fault_evt_ui();
                }
                RsPanelEndpoint_QueueSysReadyNotify();
                /* Фон: последние 11 событий журнала (GET + GET_N×3). */
                PanelJournalCache_OnSysReady();
            }
            /* Сессия конфигурации закончилась — снять оверлей (иначе tick блочит UI). */
            if (prev_cfg != 0u && s_config_active == 0u && poll.has_config == 0u) {
                MenuConfig_SetRemoteStatus(MENU_CFG_STATE_IDLE, 0u);
            }
        }
    }

    if (s_fire_active != 0u || s_config_active != 0u) {
        MenuUi_SetMainScreenActive(1u);
    }

    if (poll.has_time != 0u) {
        RTC_TimeTypeDef time = {0};
        RTC_DateTypeDef date = {0};
        time.Hours = poll.time.hour;
        time.Minutes = poll.time.min;
        time.Seconds = poll.time.sec;
        date.Date = poll.time.day;
        date.Month = poll.time.month;
        date.Year = poll.time.year;
        date.WeekDay = RTC_WEEKDAY_MONDAY;
        (void)HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
        (void)HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);
        /* UI читает RtcCache (Model::tick), не сырой RTC — иначе до Tick1s 00:00. */
        RtcCache_Refresh();
    }

    if (poll.has_zone_names != 0u) {
        rs_v3_apply_zone_names(&poll.zone_names);
    }
    if (poll.has_devices != 0u) {
        rs_v3_apply_devices(&poll.device);
    }
    if (poll.has_fault_evt != 0u) {
        rs_v3_apply_fault_evt(&poll.fault_evt);
    }
    if (poll.has_zones != 0u) {
        rs_v3_apply_zones(&poll.zones);
    }

    if (poll.has_event_reply != 0u) {
        const RsPanelV3EventReply *er = &poll.event_reply;
        if (er->type == (uint8_t)RS_PANEL_V3_EVT_JOURNAL_COUNT &&
            er->payload_len >= 4u) {
            /* Не SetList(..., 0 items) — это стирало окно до прихода JOURNAL_GET. */
            PanelJournalCache_SetTotal(rs_v3_get_u32le(er->payload));
        } else if ((er->type == (uint8_t)RS_PANEL_V3_EVT_JOURNAL_GET ||
                    er->type == (uint8_t)RS_PANEL_V3_EVT_JOURNAL_GET_N) &&
                   er->payload_len >= 13u) {
            const uint8_t *p = er->payload;
            uint32_t total = rs_v3_get_u32le(&p[0]);
            uint32_t selected = rs_v3_get_u32le(&p[4]);
            uint32_t window_first = rs_v3_get_u32le(&p[8]);
            uint8_t n_items = p[12];
            uint16_t items_len = (uint16_t)(er->payload_len - 13u);
            if (n_items != 0u && items_len != 0u) {
                PanelJournalCache_SetList(total, selected, window_first, n_items,
                                          &p[13], items_len);
            } else {
                PanelJournalCache_SetList(total, selected, window_first, 0u, 0, 0u);
            }
        } else if (er->type == (uint8_t)RS_PANEL_V3_EVT_EXT_CAN_SET &&
                   er->payload_len >= 1u) {
            PanelConnectionCache_SetRemoteStatus(EspManager_IsUserWifiOn(),
                                                 (er->payload[0] != 0u) ? 1u : 0u);
        }
        if (s_evt_q_n == 0u && s_start_all_evt_pending == 0u &&
            s_fire_reset_evt_pending == 0u &&
            (s_event.seq == 0u || rs_v3_evt_is_fire_ctrl(s_event.type) == 0u)) {
            PanelJournalCache_PrefetchProcess();
        }
    }

    if (poll.has_config != 0u) {
        if (poll.config.phase == (uint8_t)RS_PANEL_V3_CFG_SUCCESS) {
            Beeper_StartPulseTrain(100u, 100u, 3u, 0u);
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_SUCCESS, 100u);
        } else if (poll.config.phase == (uint8_t)RS_PANEL_V3_CFG_FAIL) {
            Beeper_StartPulseTrain(SOUND_FAULT_DUTY_ON_MS, SOUND_FAULT_DUTY_OFF_MS,
                                   SOUND_FAULT_DUTY_PULSES, 0u);
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_IDLE, poll.config.percent);
        } else if (poll.config.phase == (uint8_t)RS_PANEL_V3_CFG_RUNNING) {
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_RECEIVING, poll.config.percent);
        } else {
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_IDLE, 0u);
        }
    }

    if (s_sys_ready != 0u && s_fault_ui_dirty == 0u) {
        if (s_has_faults != 0u && s_active_fault_count == 0u) {
            /* Ждём FAULT_SET — без placeholder «НЕИСП». */
            rs_v3_wait_fault_evt_ui();
        } else if (s_has_faults == 0u &&
                   (s_active_fault_count > 0u ||
                    s_fault_placeholder_active != 0u ||
                    s_fault_ui_pushed_count > 0u)) {
            /* HAS_FAULTS=0, а список/UI ещё живы: master мог не дослать CLEAR
             * (CRC уже empty после частичного CLEAR). Safety — очистить локально. */
            if (s_active_fault_count > 0u) {
                s_active_fault_count = 0u;
                memset(s_active_faults, 0, sizeof(s_active_faults));
                rs_v3_recalc_catalog_crc();
                Led_Set(LED_ERR, 0u);
            }
            s_fault_placeholder_active = 0u;
            s_fault_sound_resync = 0u;
            s_fault_pre_clear_count = 0u;
            Beeper_StopPattern();
            s_fault_sound_phase = RS_V3_FS_IDLE;
            s_fault_sound_deadline_ms = 0u;
            rs_v3_push_faults_ui();
        }
    }
    return 1u;
}

uint16_t RsPanelV3Slave_BuildRsp(uint8_t *dst, uint16_t dst_size)
{
    RsPanelV3Rsp rsp;
    rs_v3_evt_retry_sticky();
    rs_v3_evt_flush_queue();
    memset(&rsp, 0, sizeof(rsp));
    rsp.event = s_event;
    rsp.zones_crc = s_zones_crc;
    rsp.devices_crc = s_devices_crc;
    rsp.faults_crc = s_faults_crc;
    rsp.flags = s_rsp_flags;
    return RsPanelV3_EncodeRsp(dst, dst_size, &rsp);
}

void RsPanelV3Slave_OnButtonSample(void)
{
    ButtonState st_all = Button_GetState(BUT_FORCE);
    ButtonState st_stop = Button_GetState(BUT_STOP);
    uint8_t all_down = (st_all == ButtonStatePress || st_all == ButtonStateLongPress) ? 1u : 0u;
    uint8_t stop_down = (st_stop == ButtonStatePress || st_stop == ButtonStateLongPress) ? 1u : 0u;
    uint32_t now = HAL_GetTick();

    /* ПУСК ОБЩИЙ hold — локально. */
    if (all_down != 0u) {
        if (s_hold_start_all == 0u) {
            s_hold_start_all = 1u;
            s_hold_start_all_ms = 0u;
            s_hold_start_all_committed = 0u;
            s_blink_on = 1u;
            s_blink_last_ms = now;
            Led_SetBrightness(LED_BUT_START_ALL, LED_BUT_MAX_BRIGHTNESS);
            Led_SetBrightness(LED_STR_START_ALL, LED_BUT_MAX_BRIGHTNESS);
            Led_Set(LED_BUT_START_ALL, 1u);
            Led_Set(LED_STR_START_ALL, 1u);
            Beeper_StartPulseTrain(SOUND_START_ALL_HOLD_DUTY_MS,
                                   SOUND_START_ALL_HOLD_PERIOD_MS - SOUND_START_ALL_HOLD_DUTY_MS,
                                   1u, SOUND_START_ALL_HOLD_PERIOD_MS);
            /* Не ставить main=1 здесь: иначе FrontendApplication решит, что
             * уже на главном, и не сделает GotoScreen — таймер 3с не виден.
             * Переход: Fire_IsStartAllHoldActive() в handleTickEvent. */
        } else if (s_hold_start_all_ms < RS_V3_HOLD_START_ALL_MS) {
            s_hold_start_all_ms = (uint16_t)(s_hold_start_all_ms + 10u);
        }
        if (s_hold_start_all_ms >= RS_V3_HOLD_START_ALL_MS &&
            s_hold_start_all_committed == 0u) {
            s_hold_start_all_committed = 1u;
            Beeper_StopPattern();
            rs_v3_restore_fault_beep_if_needed();
            rs_v3_restore_start_all_idle_leds();
            /* EVT обязателен: пока слот занят — pending + retry каждый 10 мс. */
            if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_START_ALL_COMMIT, 0u, 0u, 0u, 0u) == 0u) {
                s_start_all_evt_pending = 1u;
            } else {
                s_start_all_evt_pending = 0u;
            }
        }
        if ((now - s_blink_last_ms) >= RS_V3_BLINK_HALF_MS &&
            s_hold_start_all_committed == 0u) {
            s_blink_last_ms = now;
            s_blink_on ^= 1u;
            Led_Set(LED_BUT_START_ALL, s_blink_on);
            Led_Set(LED_STR_START_ALL, s_blink_on);
        }
    } else if (s_hold_start_all != 0u) {
        s_hold_start_all = 0u;
        s_hold_start_all_ms = 0u;
        if (s_hold_start_all_committed == 0u) {
            Beeper_StopPattern();
            rs_v3_restore_fault_beep_if_needed();
        }
        s_hold_start_all_committed = 0u;
        rs_v3_restore_start_all_idle_leds();
    }

    /* ОСТАНОВ hold ≥5 с → FIRE_RESET. */
    if (stop_down != 0u && (s_fire_active != 0u || s_hold_reset != 0u)) {
        if (s_hold_reset == 0u) {
            s_hold_reset = 1u;
            s_hold_reset_ms = 0u;
            s_hold_reset_committed = 0u;
        } else if (s_hold_reset_ms < RS_V3_HOLD_FIRE_RESET_MS) {
            s_hold_reset_ms = (uint16_t)(s_hold_reset_ms + 10u);
        }
        if (s_hold_reset_ms >= RS_V3_HOLD_FIRE_RESET_MS &&
            s_hold_reset_committed == 0u) {
            s_hold_reset_committed = 1u;
            if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_FIRE_RESET, 0u, 0u, 0u, 0u) == 0u) {
                s_fire_reset_evt_pending = 1u;
            } else {
                s_fire_reset_evt_pending = 0u;
            }
        }
    } else if (s_hold_reset != 0u) {
        s_hold_reset = 0u;
        s_hold_reset_ms = 0u;
        s_hold_reset_committed = 0u;
        /* pending FIRE_RESET не сбрасываем — дошлём после отпускания. */
    }

    rs_v3_evt_retry_sticky();
}

void RsPanelV3Slave_DrainPanelState(PanelStateContext *ctx)
{
    uint8_t i;
    uint16_t screen;
    if (ctx == 0 || s_v3_active == 0u) {
        return;
    }
    screen = ctx->current_screen;

    for (i = 0u; i < ctx->pending_ui_count && i < RS_PANEL_MAX_POLL_UI_EVENTS; i++) {
        const RsPanelUiEvent *ue = &ctx->pending_ui[i];
        if (ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_FIRE_SELECT) {
            if (ue->p1 != 0u && ue->p2 < s_zones.count) {
                s_selected_zone = s_zones.items[ue->p2].zone;
            } else {
                s_selected_zone = 0u;
            }
            continue;
        }
        if (screen == RS_PANEL_SCREEN_MENU_JOURNAL ||
            screen == RS_PANEL_SCREEN_MENU_JOURNAL_DETAIL) {
            if (ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_NAV) {
                /* Локальный FIFO: сразу selected, prefetch края по RS. */
                PanelJournalCache_Navigate((uint8_t)ue->p1);
            } else if (ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_CONFIRM) {
                PanelJournalCache_JumpNewest();
            }
        } else if (screen == RS_PANEL_SCREEN_MENU_ROOT &&
                   ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_MENU_SELECT) {
            if (ue->p1 == 3u) {
                PanelJournalCache_Open();
            } else if (ue->p1 == 4u) {
                (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_DEVICES_COUNT, 0u, 0u, 0u, 0u);
            } else if (ue->p1 == 5u) {
                (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_ZONE_BLOCK_LIST, 0u, 0u, 0u, 0u);
            }
        } else if (screen == RS_PANEL_SCREEN_MENU_CONNECTION &&
                   ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_CONFIRM) {
            uint8_t sel = MenuUi_GetConnectionSelected();
            if (sel == 0u) {
                uint8_t on = (EspManager_IsUserWifiOn() != 0u) ? 0u : 1u;
                /* Оптимистично: «Вкл/Выкл» и мигание значка до следующего POLL. */
                if (MenuUi_IsWifiBlocked() == 0u) {
                    uint8_t host = EspManager_IsHostConnected();
                    uint8_t session = (uint8_t)(on != 0u || host != 0u);
                    PanelConnectionCache_SetRemoteStatus(
                        on, PanelHostCache_GetConst()->rs485_on);
                    PanelEspManager_SetRemoteStatus(session, host, host, session);
                    (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_WIFI_SET, 0u, on, 0u, 0u);
                }
            } else {
                uint8_t on = (PanelHostCache_GetConst()->rs485_on != 0u) ? 0u : 1u;
                PanelConnectionCache_SetRemoteStatus(EspManager_IsUserWifiOn(), on);
                (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_EXT_CAN_SET, 0u, on, 0u, 0u);
            }
        } else if (screen == RS_PANEL_SCREEN_MENU_SOUND &&
                   ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_CONFIRM) {
            uint8_t on = (MenuUi_GetSoundValue() != 0u) ? 0u : 1u;
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_SOUND_SET, 0u, on, 0u, 0u);
        } else if (screen == RS_PANEL_SCREEN_MENU_BLOCK_ZONE &&
                   ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_CONFIRM) {
            uint8_t zi = MenuUi_GetBlockZoneSelected();
            uint8_t zone = (uint8_t)(zi + 1u);
            uint8_t blocked = 1u;
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_ZONE_BLOCK_SET, zone, blocked, 0u, 0u);
        } else if (screen == RS_PANEL_SCREEN_MENU_DEVICES &&
                   ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_CONFIRM) {
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_DEVICES_GET, 0u, 0u,
                                           (uint16_t)MenuUi_GetMcuDetailSlot(), 0u);
        }
    }

    for (i = 0u; i < ctx->pending_btn_count && i < RS_PANEL_MAX_POLL_BTN_EVENTS; i++) {
        const RsPanelButtonEvent *be = &ctx->pending_btn[i];
        if (be->state != (uint8_t)RS_PANEL_BUTTON_PRESS) {
            continue;
        }
        if (be->type == (uint8_t)RS_PANEL_BTN_START_SP) {
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_START_SP, s_selected_zone, 0u, 0u, 0u);
        } else if (be->type == (uint8_t)RS_PANEL_BTN_STOP) {
            const uint8_t posted =
                RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_STOP_LAUNCH, s_selected_zone, 0u, 0u, 0u);
            /* Optimistic UI только если EVT ушёл/в очереди — иначе рассинхрон с хостом. */
            if (posted != 0u && s_zones_valid != 0u && s_zones.count > 0u) {
                uint8_t zi;
                uint8_t changed = 0u;
                for (zi = 0u; zi < s_zones.count && zi < RS_PANEL_V3_MAX_ZONES; zi++) {
                    RsPanelV3ZoneItem *it = &s_zones.items[zi];
                    if (s_selected_zone != 0u && it->zone != s_selected_zone) {
                        continue;
                    }
                    if (it->status == (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN) {
                        it->status = (uint8_t)RS_PANEL_V3_ZONE_PAUSED;
                        changed = 1u;
                    } else if (it->status == (uint8_t)RS_PANEL_V3_ZONE_PAUSED) {
                        it->status = (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN;
                        changed = 1u;
                    }
                }
                if (changed != 0u) {
                    rs_v3_apply_zones(&s_zones);
                }
            }
        }
    }

    ctx->pending_ui_count = 0u;
    ctx->pending_btn_count = 0u;

    /* Журнал не занимает слот, пока ждут большие кнопки. */
    if (s_evt_q_n == 0u && s_start_all_evt_pending == 0u &&
        s_fire_reset_evt_pending == 0u &&
        (s_event.seq == 0u || rs_v3_evt_is_fire_ctrl(s_event.type) == 0u)) {
        PanelJournalCache_PrefetchProcess();
    }
}

static void rs_v3_sync_power_led(void)
{
    /* Как ППКУ1 App_UpdatePowerFaultIndication: состояние приходит с хоста в SYS. */
    if (s_v3_active == 0u) {
        Led_Set(LED_POWER, 0u);
        s_led_power_toggle_cnt = 0u;
        return;
    }

    if (s_power_input_fault != 0u) {
        /* Период 2 с, фаза ON/OFF по половине — как App_UpdatePowerFaultIndication. */
        const uint16_t period_ticks =
            (uint16_t)(LED_POWER_TOOGLE_PERIOD_MS / 10u); /* Timer10ms */
        const uint16_t half_ticks = (uint16_t)(period_ticks / 2u);
        if (s_led_power_toggle_cnt != 0u) {
            if (s_led_power_toggle_cnt == half_ticks) {
                Led_Set(LED_POWER, 1u);
            }
            s_led_power_toggle_cnt--;
        } else {
            Led_Set(LED_POWER, 0u);
            s_led_power_toggle_cnt = period_ticks;
        }
    } else {
        Led_Set(LED_POWER, 1u);
        s_led_power_toggle_cnt = (uint16_t)(LED_POWER_TOOGLE_PERIOD_MS / 10u);
    }
}

static void rs_v3_sync_status_leds(void)
{
    uint8_t fire = s_fire_active;
    uint8_t link_lost = PanelLinkMonitor_IsLost();
    /* LED_ERR только по реальному списку / link lost — не по SYS_HAS_FAULTS
     * без FAULT_SET (иначе лампа горит, а на экране ещё «НОРМА»). */
    uint8_t faults = (s_active_fault_count > 0u || link_lost != 0u) ? 1u : 0u;

    rs_v3_sync_power_led();

    if (fire != 0u || faults != 0u) {
        Led_Set(LED_NORM, 0u);
    } else if (s_sys_ready != 0u) {
        Led_Set(LED_NORM, 1u);
    } else {
        Led_Set(LED_NORM, 0u);
    }

    if (fire != 0u) {
        Led_Set(LED_FIRE, 1u);
        Led_SetBrightness(LED_FIRE, LED_STATUS_MAX_BRIGHTNESS);
        Led_ForceStatusBright(LED_FIRE);
        /* LED_START — rs_v3_sync_fire_indication (тушение). */
    } else {
        Led_Set(LED_FIRE, 0u);
        if (s_fire_snd_phase == RS_V3_FIRE_SND_IDLE) {
            Led_Set(LED_START, 0u);
        }
        Led_Set(LED_STOP, 0u);
    }

    if (faults != 0u) {
        Led_Set(LED_ERR, 1u);
        Led_ForceStatusBright(LED_ERR);
    } else {
        Led_Set(LED_ERR, 0u);
    }
}

void RsPanelV3Slave_RequestFaultUiRefresh(void)
{
    if (PanelLinkMonitor_IsLost() != 0u) {
        return;
    }
    if (s_sys_ready == 0u) {
        RsPanelEndpoint_QueueWarningUi(0u, 0u, 0, 0);
        return;
    }
    if (s_active_fault_count > 0u || s_has_faults == 0u) {
        rs_v3_push_faults_ui();
    } else {
        rs_v3_wait_fault_evt_ui();
    }
}

void RsPanelV3Slave_Timer10ms(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t i;

    RsPanelV3Slave_OnButtonSample();
    rs_v3_evt_retry_sticky();
    rs_v3_evt_flush_queue();
    rs_v3_sync_status_leds();
    /* Запись каталога во Flash после quiet (не на каждый POLL). */
    if (s_catalog_flash_dirty != 0u &&
        (now - s_catalog_flash_dirty_ms) >= RS_V3_CATALOG_FLASH_QUIET_MS) {
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        rs_v3_catalog_flash_save_if_dirty();
    }
    /* Журнал не трогает слот, пока ждут большие кнопки. */
    if (s_evt_q_n == 0u && s_start_all_evt_pending == 0u &&
        s_fire_reset_evt_pending == 0u &&
        (s_event.seq == 0u || rs_v3_evt_is_fire_ctrl(s_event.type) == 0u)) {
        PanelJournalCache_PrefetchProcess();
    }

    if (s_fire_active == 0u &&
        s_fault_sound_phase == RS_V3_FS_WAIT_DUTY &&
        (int32_t)(now - s_fault_sound_deadline_ms) >= 0) {
        rs_v3_fault_beep(rs_v3_has_attention_faults(), 0u);
    }

    /* Пакет FAULT_SET — один push после паузы; только после SYS_READY.
     * Во время CLEAR_ALL→SET (resync) не пушить placeholder при count==0 —
     * иначе UI потом отвергает 4→3 как «glitch» / остаётся старый список. */
    if (s_sys_ready != 0u && s_fault_ui_dirty != 0u &&
        (now - s_fault_ui_dirty_ms) >= RS_V3_FAULT_UI_QUIET_MS) {
        if (s_active_fault_count > 0u || s_has_faults == 0u) {
            const uint8_t was_resync = s_fault_sound_resync;
            s_fault_sound_resync = 0u;
            s_fault_resync_wait_ms = 0u;
            rs_v3_push_faults_ui();
            if (was_resync != 0u) {
                rs_v3_fault_sound_after_resync();
            }
        } else if (s_fault_sound_resync != 0u) {
            /* Ждём SET после CLEAR_ALL (макс. 3 с). Не пушить «НЕИСП».
             * По таймауту сбрасываем UI (count==0), CRC на master дошлёт SET. */
            if (s_fault_resync_wait_ms == 0u) {
                s_fault_resync_wait_ms = now;
            }
            if ((now - s_fault_resync_wait_ms) < 3000u) {
                s_fault_ui_dirty_ms = now;
            } else {
                s_fault_sound_resync = 0u;
                s_fault_resync_wait_ms = 0u;
                rs_v3_push_faults_ui();
                rs_v3_fault_sound_after_resync();
            }
        } else {
            /* HAS_FAULTS, списка ещё нет — ждать SET, не показывать «НЕИСП». */
            s_fault_ui_dirty_ms = now;
        }
    }

    /* Локальный тик таймеров зон раз в 1 с. */
    if (s_local_timer_tick_ms == 0u) {
        s_local_timer_tick_ms = now;
    }
    if ((now - s_local_timer_tick_ms) >= 1000u) {
        s_local_timer_tick_ms += 1000u;
        for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
            if ((s_zones.items[i].status == (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN) &&
                s_local_timer_s[i] > 0u) {
                s_local_timer_s[i]--;
            }
        }
        if (s_zones_valid != 0u && s_zones.count > 0u) {
            rs_v3_apply_zones(&s_zones);
        }
    }
}
