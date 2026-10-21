#include "rs_panel_v3_slave.h"

#include "rs_panel_protocol.h"
#include "button.h"
#include "led.h"
#include "beeper.h"
#include "sound_profiles.h"
#include "panel_journal_cache.h"
#include "panel_link_monitor.h"
#include "rs_panel_endpoint.h"
#include "menu_ui.h"
#include "esp_manager.h"
#include "device_config.h"
#include "panel_cfg.h"
#include "panel_host_cache.h"
#include "panel_catalog_cache.h"
#include "config_zone_block.h"
#include "fire.h"
#include "rtc_cache.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

extern void SaveConfig(void);

#define RS_V3_HOLD_START_ALL_MS     3000u /* ПУСК ОБЩИЙ: отсчёт на экране */
#define RS_V3_SP_ARM_MS             2000u /* ПУСК СП (большая): hold ≥2с → без EVT */
#define RS_V3_SP_COUNTDOWN_MS       3000u /* ПУСК СП (малая): отсчёт = как ОБЩИЙ */
#define RS_V3_HOLD_FIRE_RESET_MS    5000u /* ОСТАНОВ: сброс пожара */
#define RS_V3_BLINK_HALF_MS         800u
/* После последнего ZONE/DEV TLV — сохранить Flash (не на каждый POLL). */
#define RS_V3_CATALOG_FLASH_QUIET_MS 400u
/* Fire-ctrl: sticky тот же seq до ACK доставки. Эффект (тушить/нет) — только ППКУ. */
#define RS_V3_EVT_INFLIGHT_TIMEOUT_MS 1500u
/* HAS_FAULTS=0 при живом UI — не мигать «НОРМА» на один POLL. */
#define RS_V3_FAULT_CLEAR_SAFE_MS   500u

/* >>> RS_V3_DBG_START_ALL_LAG BEGIN
 * TEMP отладка лага ПУСК ОБЩИЙ: удалить ВСЁ между BEGIN/END (поиск: RS_V3_DBG_START_ALL_LAG).
 * Live Expressions / Watch:
 *   g_rs_v3_dbg_sa.post_ms / first_build_ms / ack_ms
 *   g_rs_v3_dbg_sa.dt_post_to_build_ms  — пост EVT → первый BuildRsp с COMMIT
 *   g_rs_v3_dbg_sa.dt_build_to_ack_ms   — первый BuildRsp → ACK с хоста
 *   g_rs_v3_dbg_sa.dt_post_to_ack_ms    — пост → ACK
 *   g_rs_v3_dbg_sa.post_ok (1=в слоте), .post_seq, .post_pending, .q_n
 *   g_rs_v3_dbg_sa.build_hits — сколько BuildRsp ушли с COMMIT до ACK
 * Breakpoint: rs_v3_dbg_sa_mark — смотреть stage: 1=post, 2=first BuildRsp, 3=ACK.
 * <<< */
typedef struct {
    volatile uint32_t post_ms;
    volatile uint32_t first_build_ms;
    volatile uint32_t ack_ms;
    volatile uint32_t dt_post_to_build_ms;
    volatile uint32_t dt_build_to_ack_ms;
    volatile uint32_t dt_post_to_ack_ms;
    volatile uint32_t build_hits;
    volatile uint8_t  post_ok;
    volatile uint8_t  post_seq;
    volatile uint8_t  post_type;
    volatile uint8_t  post_pending;
    volatile uint8_t  q_n;
    volatile uint8_t  first_build_seq;
    volatile uint8_t  ack_seq;
    volatile uint8_t  stage; /* 0 idle, 1 post, 2 build, 3 ack */
} RsV3DbgStartAllLag;

volatile RsV3DbgStartAllLag g_rs_v3_dbg_sa;

static void rs_v3_dbg_sa_mark(uint8_t stage)
{
    g_rs_v3_dbg_sa.stage = stage;
    /* BP здесь: stage 1/2/3 + поля g_rs_v3_dbg_sa.* */
    (void)stage;
}
/* >>> RS_V3_DBG_START_ALL_LAG END */

static uint8_t s_sys_ready;
static uint8_t s_fire_active;
static uint8_t s_config_active;
static uint8_t s_has_faults;
static uint8_t s_power_input_fault; /* SYS POWER_INPUT_FAULT → мигание LED_POWER */
static uint16_t s_led_power_toggle_cnt; /* тики 10 мс; период = LED_POWER_TOOGLE_PERIOD_MS */

static RsPanelV3Event s_event;           /* ordinary lane inflight */
static RsPanelV3Event s_event_crit;      /* critical lane inflight */
static uint8_t s_event_seq_ctr;          /* общий счётчик seq для обеих линий */
static uint32_t s_event_inflight_ms;      /* HAL tick поста в s_event; 0 = пуст */
static uint32_t s_event_crit_inflight_ms; /* HAL tick поста в s_event_crit; 0 = пуст */
static uint16_t s_zones_apply_gen;   /* ++ на каждый from_host ZONES */

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
static uint32_t s_fault_clear_safe_ms; /* старт debounce HAS_FAULTS=0 → wipe UI */

/* Локальные фазы звука/LED пожара (v3: CMD_SOUND/LED с хоста нет). */
#define RS_V3_FIRE_SND_IDLE   0u
#define RS_V3_FIRE_SND_ALERT  1u  /* сигнал ПОЖАР */
#define RS_V3_FIRE_SND_START  2u  /* тушение → паттерн ПУСК + LED_START */
#define RS_V3_FIRE_SND_DUTY   3u  /* ТУШ.ВЫП / дежурный */
static uint8_t s_fire_snd_phase;

/* Две независимые EVT-линии (NO preemption между ними).
 * Ordinary: s_event / s_evt_q — журнал/меню/wifi/звук.
 * Critical: s_event_crit / s_evt_q_crit — START_SP/STOP/START_ALL/FIRE_RESET.
 * Общий s_event_seq_ctr — уникальный seq across lanes. */
#define RS_V3_EVT_Q_MAX 12u
static RsPanelV3Event s_evt_q[RS_V3_EVT_Q_MAX];
static uint8_t s_evt_q_n;
static RsPanelV3Event s_evt_q_crit[RS_V3_EVT_Q_MAX];
static uint8_t s_evt_q_crit_n;
/* Повтор PostEvent, пока слот/очередь не приняли (только доставка до ACK). */
static uint8_t s_start_all_evt_pending;
static uint8_t s_fire_reset_evt_pending;
static uint8_t s_fire_reset_evt_zone; /* zone на момент commit RESET */
static uint8_t s_start_sp_evt_pending;
static uint8_t s_stop_launch_evt_pending;
/* Уровни кнопок (антидребезг / SM). */
static uint8_t s_btn_start_sp_down;
static uint8_t s_btn_stop_evt_down;
/* ПУСК СП: малая — сразу UI-таймер 3с → START_ALL_COMMIT; отпуск раньше → START_SP.
 * Большая: RELEASE <2с → START_SP; hold ≥2с → без EVT. */
static uint8_t s_hold_start_sp;          /* кнопка зажата (малая) */
static uint16_t s_hold_start_sp_ms;      /* длительность зажатия FIRE (малая/большая) */
static uint8_t s_hold_start_sp_cd;       /* фаза UI-таймера (малая) */
static uint16_t s_hold_start_sp_cd_ms;
static uint8_t s_hold_start_sp_committed;
/* ОСТАНОВ: RELEASE <5с → STOP (пауза/resume); hold ≥5с при пожаре → RESET. */
static uint16_t s_stop_press_ms;

static void rs_v3_restore_start_all_idle_leds(void);
static void rs_v3_restore_beep_after_hold_cancel(void);
static void rs_v3_cancel_local_holds(void);
static void rs_v3_push_fire_ui_clear(void);
static void rs_v3_post_start_sp(void);
static void rs_v3_post_start_all_commit(void);
static void rs_v3_post_stop_launch(void);
static void rs_v3_cancel_start_sp(void);
static void rs_v3_cancel_stop(void);
static void rs_v3_evt_q_crit_remove_type_zone(uint8_t type, uint8_t zone);
static void rs_v3_force_fire_event(uint8_t type, uint8_t zone);
static void rs_v3_evt_inflight_watchdog(void);
static void rs_v3_apply_zones(const RsPanelV3Zones *z);
static uint8_t rs_v3_has_attention_faults(void);
static uint8_t rs_v3_zone_is_terminal(uint8_t st);
static uint8_t rs_v3_fire_in_post_extinguish_duty(void);
static uint8_t rs_v3_allow_fault_signal_now(void);
static void rs_v3_fault_beep(uint8_t attn, uint8_t signal);
static void rs_v3_on_fault_signal_done(void);
static void rs_v3_fault_sound_after_resync(void);
static void rs_v3_sync_led_stop_status(void);
static void rs_v3_sync_fire_indication(void);
static void rs_v3_evt_flush_queue(void);
static void rs_v3_evt_flush_crit(void);
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
    memset(&s_event_crit, 0, sizeof(s_event_crit));
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
    s_event_inflight_ms = 0u;
    s_event_crit_inflight_ms = 0u;
    s_zones_apply_gen = 0u;
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
    s_fault_clear_safe_ms = 0u;
    s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
    s_evt_q_n = 0u;
    memset(s_evt_q, 0, sizeof(s_evt_q));
    s_evt_q_crit_n = 0u;
    memset(s_evt_q_crit, 0, sizeof(s_evt_q_crit));
    s_start_all_evt_pending = 0u;
    s_fire_reset_evt_pending = 0u;
    s_fire_reset_evt_zone = 0u;
    s_start_sp_evt_pending = 0u;
    s_stop_launch_evt_pending = 0u;
    s_btn_start_sp_down = 0u;
    s_btn_stop_evt_down = 0u;
    s_hold_start_sp = 0u;
    s_hold_start_sp_ms = 0u;
    s_hold_start_sp_cd = 0u;
    s_hold_start_sp_cd_ms = 0u;
    s_hold_start_sp_committed = 0u;
    s_stop_press_ms = 0u;
    s_stop_press_ms = 0u;
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
uint8_t RsPanelV3Slave_IsHoldActive(void)
{
    /* UI-таймер: ПУСК ОБЩИЙ или фаза countdown ПУСК СП (малая). */
    if (s_hold_start_all != 0u && s_hold_start_all_committed == 0u) {
        return 1u;
    }
    if (s_hold_start_sp_cd != 0u && s_hold_start_sp_committed == 0u) {
        return 1u;
    }
    return 0u;
}

uint8_t RsPanelV3Slave_GetHoldRemainingSec(void)
{
    uint16_t left_ms;
    if (s_hold_start_all != 0u && s_hold_start_all_committed == 0u) {
        if (s_hold_start_all_ms >= RS_V3_HOLD_START_ALL_MS) {
            return 0u;
        }
        left_ms = (uint16_t)(RS_V3_HOLD_START_ALL_MS - s_hold_start_all_ms);
        return (uint8_t)((left_ms + 999u) / 1000u);
    }
    if (s_hold_start_sp_cd != 0u && s_hold_start_sp_committed == 0u) {
        if (s_hold_start_sp_cd_ms >= RS_V3_SP_COUNTDOWN_MS) {
            return 0u;
        }
        left_ms = (uint16_t)(RS_V3_SP_COUNTDOWN_MS - s_hold_start_sp_cd_ms);
        return (uint8_t)((left_ms + 999u) / 1000u);
    }
    return 0u;
}

static uint8_t rs_v3_evt_is_fire_ctrl(uint8_t type)
{
    return (type == (uint8_t)RS_PANEL_V3_EVT_START_SP ||
            type == (uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH ||
            type == (uint8_t)RS_PANEL_V3_EVT_START_ALL_COMMIT ||
            type == (uint8_t)RS_PANEL_V3_EVT_FIRE_RESET) ? 1u : 0u;
}

static void rs_v3_evt_next_seq(uint8_t *out_seq)
{
    if (s_event_seq_ctr == 0u) {
        s_event_seq_ctr = 1u;
    }
    *out_seq = s_event_seq_ctr++;
    if (s_event_seq_ctr == 0u) {
        s_event_seq_ctr = 1u;
    }
}

static void rs_v3_evt_assign(uint8_t type, uint8_t zone, uint8_t u8_a,
                             uint16_t u16_a, uint16_t u16_b)
{
    rs_v3_evt_next_seq(&s_event.seq);
    s_event.type = type;
    s_event.zone = zone;
    s_event.u8_a = u8_a;
    s_event.u16_a = u16_a;
    s_event.u16_b = u16_b;
    s_event_inflight_ms = HAL_GetTick();
    if (s_event_inflight_ms == 0u) {
        s_event_inflight_ms = 1u;
    }
}

static void rs_v3_evt_assign_crit(uint8_t type, uint8_t zone, uint8_t u8_a,
                                  uint16_t u16_a, uint16_t u16_b)
{
    rs_v3_evt_next_seq(&s_event_crit.seq);
    s_event_crit.type = type;
    s_event_crit.zone = zone;
    s_event_crit.u8_a = u8_a;
    s_event_crit.u16_a = u16_a;
    s_event_crit.u16_b = u16_b;
    s_event_crit_inflight_ms = HAL_GetTick();
    if (s_event_crit_inflight_ms == 0u) {
        s_event_crit_inflight_ms = 1u;
    }
}

static void rs_v3_evt_clear_inflight(void)
{
    s_event.seq = 0u;
    s_event.type = (uint8_t)RS_PANEL_V3_EVT_NONE;
    s_event_inflight_ms = 0u;
}

static void rs_v3_evt_clear_inflight_crit(void)
{
    s_event_crit.seq = 0u;
    s_event_crit.type = (uint8_t)RS_PANEL_V3_EVT_NONE;
    s_event_crit_inflight_ms = 0u;
}

static void rs_v3_evt_q_crit_remove_type_zone(uint8_t type, uint8_t zone)
{
    uint8_t i = 0u;
    while (i < s_evt_q_crit_n) {
        if (s_evt_q_crit[i].type == type && s_evt_q_crit[i].zone == zone) {
            uint8_t j;
            for (j = (uint8_t)(i + 1u); j < s_evt_q_crit_n; j++) {
                s_evt_q_crit[j - 1u] = s_evt_q_crit[j];
            }
            s_evt_q_crit_n--;
            continue;
        }
        i++;
    }
}

static void rs_v3_cancel_start_sp(void)
{
    uint8_t z = s_event_crit.zone;
    s_start_sp_evt_pending = 0u;
    if (s_event_crit.seq != 0u &&
        s_event_crit.type == (uint8_t)RS_PANEL_V3_EVT_START_SP) {
        rs_v3_evt_clear_inflight_crit();
    }
    rs_v3_evt_q_crit_remove_type_zone((uint8_t)RS_PANEL_V3_EVT_START_SP, z);
    rs_v3_evt_q_crit_remove_type_zone((uint8_t)RS_PANEL_V3_EVT_START_SP, 0u);
    rs_v3_evt_flush_crit();
}

static void rs_v3_cancel_stop(void)
{
    uint8_t z = s_event_crit.zone;
    s_stop_launch_evt_pending = 0u;
    if (s_event_crit.seq != 0u &&
        s_event_crit.type == (uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH) {
        rs_v3_evt_clear_inflight_crit();
    }
    rs_v3_evt_q_crit_remove_type_zone((uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH, z);
    rs_v3_evt_q_crit_remove_type_zone((uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH, 0u);
    rs_v3_evt_flush_crit();
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

static uint8_t rs_v3_evt_q_crit_find(uint8_t type, uint8_t zone)
{
    uint8_t i;
    for (i = 0u; i < s_evt_q_crit_n; i++) {
        if (s_evt_q_crit[i].type == type && s_evt_q_crit[i].zone == zone) {
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

static void rs_v3_evt_q_crit_write(uint8_t idx, uint8_t type, uint8_t zone, uint8_t u8_a,
                                   uint16_t u16_a, uint16_t u16_b)
{
    s_evt_q_crit[idx].type = type;
    s_evt_q_crit[idx].zone = zone;
    s_evt_q_crit[idx].u8_a = u8_a;
    s_evt_q_crit[idx].u16_a = u16_a;
    s_evt_q_crit[idx].u16_b = u16_b;
    s_evt_q_crit[idx].seq = 0u;
}

/* Ordinary: при переполнении — выкинуть голову (FIFO). */
static void rs_v3_evt_q_make_space(void)
{
    uint8_t i;

    if (s_evt_q_n < RS_V3_EVT_Q_MAX) {
        return;
    }
    for (i = 1u; i < s_evt_q_n; i++) {
        s_evt_q[i - 1u] = s_evt_q[i];
    }
    s_evt_q_n--;
}

static void rs_v3_evt_q_crit_make_space(void)
{
    uint8_t i;

    if (s_evt_q_crit_n < RS_V3_EVT_Q_MAX) {
        return;
    }
    for (i = 1u; i < s_evt_q_crit_n; i++) {
        s_evt_q_crit[i - 1u] = s_evt_q_crit[i];
    }
    s_evt_q_crit_n--;
}

/* Ordinary — в хвост (дедуп type+zone). */
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

/* Critical — простой FIFO (дедуп type+zone), без смешивания с ordinary. */
static uint8_t rs_v3_evt_enqueue_crit_tail(uint8_t type, uint8_t zone, uint8_t u8_a,
                                           uint16_t u16_a, uint16_t u16_b)
{
    uint8_t i = rs_v3_evt_q_crit_find(type, zone);
    if (i != 0xFFu) {
        rs_v3_evt_q_crit_write(i, type, zone, u8_a, u16_a, u16_b);
        return 1u;
    }
    rs_v3_evt_q_crit_make_space();
    rs_v3_evt_q_crit_write(s_evt_q_crit_n, type, zone, u8_a, u16_a, u16_b);
    s_evt_q_crit_n++;
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

static void rs_v3_evt_flush_crit(void)
{
    uint8_t i;
    if (s_event_crit.seq != 0u || s_evt_q_crit_n == 0u) {
        return;
    }
    rs_v3_evt_assign_crit(s_evt_q_crit[0].type, s_evt_q_crit[0].zone,
                          s_evt_q_crit[0].u8_a, s_evt_q_crit[0].u16_a,
                          s_evt_q_crit[0].u16_b);
    for (i = 1u; i < s_evt_q_crit_n; i++) {
        s_evt_q_crit[i - 1u] = s_evt_q_crit[i];
    }
    s_evt_q_crit_n--;
}

uint8_t RsPanelV3Slave_PostEvent(uint8_t type, uint8_t zone, uint8_t u8_a,
                                 uint16_t u16_a, uint16_t u16_b)
{
    if (type == (uint8_t)RS_PANEL_V3_EVT_NONE) {
        return 0u;
    }

    if (rs_v3_evt_is_fire_ctrl(type) != 0u) {
        /* Critical lane only — без вытеснения ordinary. */
        if (s_event_crit.seq == 0u) {
            rs_v3_evt_assign_crit(type, zone, u8_a, u16_a, u16_b);
            return 1u;
        }
        /* Sticky same type+zone: BuildRsp повторяет seq до ACK. */
        if (s_event_crit.type == type && s_event_crit.zone == zone) {
            return 1u;
        }
        return rs_v3_evt_enqueue_crit_tail(type, zone, u8_a, u16_a, u16_b);
    }

    /* Ordinary lane only — никогда не трогает critical. */
    if (s_event.seq == 0u) {
        (void)rs_v3_evt_enqueue_tail(type, zone, u8_a, u16_a, u16_b);
        rs_v3_evt_flush_queue();
        return 1u;
    }
    return rs_v3_evt_enqueue_tail(type, zone, u8_a, u16_a, u16_b);
}

/* Fire-ctrl: всегда НОВЫЙ seq в critical слот. Только critical lane.
 * Sticky same-type без ACK давал «куча нажатий → 0 эффектов». */
static void rs_v3_force_fire_event(uint8_t type, uint8_t zone)
{
    if (type == (uint8_t)RS_PANEL_V3_EVT_NONE) {
        return;
    }
    rs_v3_evt_q_crit_remove_type_zone(type, zone);
    if (s_event_crit.seq != 0u &&
        (s_event_crit.type != type || s_event_crit.zone != zone)) {
        (void)rs_v3_evt_enqueue_crit_tail(s_event_crit.type, s_event_crit.zone,
                                          s_event_crit.u8_a, s_event_crit.u16_a,
                                          s_event_crit.u16_b);
    }
    rs_v3_evt_assign_crit(type, zone, 0u, 0u, 0u);
}

static void rs_v3_post_start_sp_raw(void)
{
    /* zone=s_selected_zone (стрелки); 0 — все awaiting на ППКУ. */
    rs_v3_force_fire_event((uint8_t)RS_PANEL_V3_EVT_START_SP, s_selected_zone);
    s_start_sp_evt_pending = 0u;
}

static void rs_v3_post_start_sp(void)
{
    /* Новый press — отменить незакрытый STOP; эффект тушения решает ППКУ. */
    rs_v3_cancel_stop();
    rs_v3_post_start_sp_raw();
}

static void rs_v3_post_start_all_commit(void)
{
    uint8_t posted;
    if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_START_ALL_COMMIT, 0u, 0u, 0u, 0u) == 0u) {
        s_start_all_evt_pending = 1u;
        posted = 0u;
    } else {
        s_start_all_evt_pending = 0u;
        posted = 1u;
    }
    /* >>> RS_V3_DBG_START_ALL_LAG BEGIN */
    {
        const uint32_t now = HAL_GetTick();
        g_rs_v3_dbg_sa.post_ms = now;
        g_rs_v3_dbg_sa.first_build_ms = 0u;
        g_rs_v3_dbg_sa.ack_ms = 0u;
        g_rs_v3_dbg_sa.dt_post_to_build_ms = 0u;
        g_rs_v3_dbg_sa.dt_build_to_ack_ms = 0u;
        g_rs_v3_dbg_sa.dt_post_to_ack_ms = 0u;
        g_rs_v3_dbg_sa.build_hits = 0u;
        g_rs_v3_dbg_sa.post_ok = posted;
        g_rs_v3_dbg_sa.post_seq = s_event_crit.seq;
        g_rs_v3_dbg_sa.post_type = s_event_crit.type;
        g_rs_v3_dbg_sa.post_pending = s_start_all_evt_pending;
        g_rs_v3_dbg_sa.q_n = s_evt_q_crit_n;
        g_rs_v3_dbg_sa.first_build_seq = 0u;
        g_rs_v3_dbg_sa.ack_seq = 0u;
        rs_v3_dbg_sa_mark(1u);
    }
    /* >>> RS_V3_DBG_START_ALL_LAG END */
}

static void rs_v3_post_stop_launch_raw(void)
{
    /* zone=s_selected_zone; 0 — пауза/resume всех countdown на ППКУ.
     * Новый seq на каждый press — иначе sticky same-seq = master no-op. */
    rs_v3_force_fire_event((uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH, s_selected_zone);
    s_stop_launch_evt_pending = 0u;
}

static void rs_v3_post_stop_launch(void)
{
    /* ОСТАНОВ отменяет незакрытый ПУСК — иначе Phase2 снимет паузу. */
    rs_v3_cancel_start_sp();
    s_start_all_evt_pending = 0u;
    rs_v3_evt_q_crit_remove_type_zone((uint8_t)RS_PANEL_V3_EVT_START_ALL_COMMIT, 0u);
    if (s_event_crit.seq != 0u &&
        s_event_crit.type == (uint8_t)RS_PANEL_V3_EVT_START_ALL_COMMIT) {
        rs_v3_evt_clear_inflight_crit();
    }
    rs_v3_post_stop_launch_raw();
}

static void rs_v3_evt_retry_sticky(void)
{
    if (s_start_all_evt_pending != 0u) {
        if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_START_ALL_COMMIT, 0u, 0u, 0u, 0u) != 0u) {
            s_start_all_evt_pending = 0u;
        }
    }
    if (s_fire_reset_evt_pending != 0u) {
        if (RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_FIRE_RESET, s_fire_reset_evt_zone,
                                     0u, 0u, 0u) != 0u) {
            s_fire_reset_evt_pending = 0u;
        }
    }
    if (s_start_sp_evt_pending != 0u) {
        rs_v3_post_start_sp_raw();
    }
    if (s_stop_launch_evt_pending != 0u) {
        rs_v3_post_stop_launch_raw();
    }
    rs_v3_evt_flush_queue();
    rs_v3_evt_flush_crit();
}

/* Inflight watchdog: ordinary — сброс зависшего fire-ctrl (не должно быть);
 * critical — START_SP/STOP/START_ALL timeout + retry. */
static void rs_v3_evt_inflight_watchdog(void)
{
    const uint32_t now = HAL_GetTick();

    if (s_event.seq != 0u && s_event_inflight_ms != 0u &&
        rs_v3_evt_is_fire_ctrl(s_event.type) != 0u &&
        (uint32_t)(now - s_event_inflight_ms) >= RS_V3_EVT_INFLIGHT_TIMEOUT_MS) {
        rs_v3_evt_clear_inflight();
        rs_v3_evt_flush_queue();
    }

    if (s_event_crit.seq == 0u || s_event_crit_inflight_ms == 0u) {
        return;
    }
    if (rs_v3_evt_is_fire_ctrl(s_event_crit.type) == 0u) {
        return;
    }
    if ((uint32_t)(now - s_event_crit_inflight_ms) < RS_V3_EVT_INFLIGHT_TIMEOUT_MS) {
        return;
    }

    /* Sticky тот же seq: BuildRsp уже повторяет EVT. Новый seq срывает
     * запоздавший ACK и при пожаре крутил watchdog без доставки. */
    s_event_crit_inflight_ms = now;
    if (s_event_crit_inflight_ms == 0u) {
        s_event_crit_inflight_ms = 1u;
    }
}

uint8_t RsPanelV3Slave_IsEventPending(void)
{
    return (s_event.seq != 0u || s_event_crit.seq != 0u) ? 1u : 0u;
}

uint8_t RsPanelV3Slave_IsEventIdle(void)
{
    return (s_event.seq == 0u && s_evt_q_n == 0u &&
            s_event_crit.seq == 0u && s_evt_q_crit_n == 0u) ? 1u : 0u;
}

static void rs_v3_restore_start_all_idle_leds(void)
{
    Led_SetBrightness(LED_BUT_START_ALL, LED_BUT_DIM_BRIGHTNESS);
    Led_SetBrightness(LED_STR_START_ALL, LED_BUT_DIM_BRIGHTNESS);
    Led_Set(LED_BUT_START_ALL, 0u);
    Led_Set(LED_STR_START_ALL, 1u);
}

/* После удержания ПУСК СП (малая): вернуть подсветку как в sync_fire_indication. */
static void rs_v3_restore_start_sp_idle_leds(void)
{
    uint8_t need_ctrl = 0u;
    uint8_t i;

    if (s_fire_active != 0u && s_zones_valid != 0u) {
        for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
            const uint8_t st = s_zones.items[i].status;
            if (st == (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN ||
                st == (uint8_t)RS_PANEL_V3_ZONE_PAUSED ||
                st == (uint8_t)RS_PANEL_V3_ZONE_FIRE_STOPPED ||
                st == (uint8_t)RS_PANEL_V3_ZONE_EXT_ABORT ||
                st == (uint8_t)RS_PANEL_V3_ZONE_FIRE1) {
                need_ctrl = 1u;
                break;
            }
        }
    }
    Led_Set(LED_BUT_START_SP, need_ctrl);
    Led_Set(LED_STR_START_SP, need_ctrl);
}

/* Сброс локальных hold UI (ПУСК ОБЩИЙ / ПУСК СП) — после FIRE_RESET / ухода пожара. */
static void rs_v3_cancel_local_holds(void)
{
    const uint8_t had_hold =
        (s_hold_start_all != 0u && s_hold_start_all_committed == 0u) ||
        (s_hold_start_sp_cd != 0u && s_hold_start_sp_committed == 0u) ? 1u : 0u;

    if (s_hold_start_all != 0u) {
        s_hold_start_all = 0u;
        s_hold_start_all_ms = 0u;
        if (s_hold_start_all_committed == 0u) {
            rs_v3_restore_start_all_idle_leds();
        }
        s_hold_start_all_committed = 0u;
    }
    if (s_hold_start_sp != 0u || s_hold_start_sp_cd != 0u) {
        s_hold_start_sp = 0u;
        s_hold_start_sp_ms = 0u;
        s_hold_start_sp_cd = 0u;
        s_hold_start_sp_cd_ms = 0u;
        s_hold_start_sp_committed = 0u;
        rs_v3_restore_start_sp_idle_leds();
    }
    if (had_hold != 0u) {
        rs_v3_restore_beep_after_hold_cancel();
    }
}

static void rs_v3_push_fire_ui_clear(void)
{
    char names[16][ZONE_NAME_SIZE + 1];
    uint8_t modes[16];
    uint8_t rems[16];

    memset(names, 0, sizeof(names));
    memset(modes, 0, sizeof(modes));
    memset(rems, 0, sizeof(rems));
    RsPanelEndpoint_QueueFireUi(0u, 0u, 0u, 0u, names, modes, rems);
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

/* ТУШ.ВЫП / ТУШ.ОШ / ТУШ.ОСТ — дежурный звук пожара; здесь новая неиспр. должна SIGNAL. */
static uint8_t rs_v3_fire_in_post_extinguish_duty(void)
{
    uint8_t i;
    uint8_t any_ext = 0u;
    uint8_t any_zone = 0u;
    uint8_t all_terminal = 1u;

    if (s_fire_active == 0u || s_zones_valid == 0u || s_zones.count == 0u) {
        return 0u;
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
    return (any_ext == 0u && any_zone != 0u && all_terminal != 0u) ? 1u : 0u;
}

static uint8_t rs_v3_allow_fault_signal_now(void)
{
    if (s_fire_active == 0u) {
        return 1u;
    }
    /* Активный ПОЖАР/тушение — fault SIGNAL не перебивает. После тушения — да. */
    return rs_v3_fire_in_post_extinguish_duty();
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

/* После SIGNAL: снова дежурный пожара (ТУШ.ВЫП) или дежурный неисправности. */
static void rs_v3_on_fault_signal_done(void)
{
    s_fault_sound_phase = RS_V3_FS_IDLE;
    s_fault_sound_deadline_ms = 0u;
    if (rs_v3_fire_in_post_extinguish_duty() != 0u) {
        /* Переиграть DUTY пожара — StopPattern уже в Beeper_StartPulseTrain/конец SIGNAL. */
        s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
        rs_v3_sync_fire_indication();
        return;
    }
    if (s_fire_active != 0u) {
        /* Успели уйти из terminal duty — фазу пожара не трогаем. */
        return;
    }
    if (s_active_fault_count == 0u) {
        return;
    }
    rs_v3_fault_beep(rs_v3_has_attention_faults(), 0u);
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

/* Отмена hold ПУСК до commit: вернуть звук пожара или дежурный fault.
 * Beeper_StopPattern глушит шину, а s_fire_snd_phase остаётся прежним —
 * sync_fire думает «уже играет» и не перезапускает. */
static void rs_v3_restore_beep_after_hold_cancel(void)
{
    Beeper_StopPattern();
    if (s_fire_active != 0u) {
        s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
        rs_v3_sync_fire_indication();
        return;
    }
    rs_v3_restore_fault_beep_if_needed();
}

/* Hold-писк в такт миганию (800/800). У Beeper repeat_ms — пауза ПОСЛЕ
 * пакета on+off; PERIOD сюда давать нельзя (получится 1 писк за 3.2 с).
 * Играет и при mute (Beeper_StartHoldPulseTrain). */
static void rs_v3_start_hold_beep(void)
{
    Beeper_StartHoldPulseTrain(SOUND_START_ALL_HOLD_DUTY_MS,
                               SOUND_START_ALL_HOLD_PERIOD_MS - SOUND_START_ALL_HOLD_DUTY_MS,
                               1u, 10u);
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
        /* После тушения (дежурный пожара) рост списка — SIGNAL, затем снова DUTY. */
        if (rs_v3_allow_fault_signal_now() != 0u &&
            s_active_fault_count > pre) {
            Beeper_ResumeSoundOnNewEvent();
            rs_v3_fault_beep(rs_v3_has_attention_faults(), 1u);
            if (MenuUi_IsMainScreenActive() == 0u || MenuUi_GetMenuSessionScreen() != 0u) {
                RsPanelEndpoint_QueueGotoMain();
            }
        }
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

/* LED_AUTO_OFF — зона mode 2/3/4 (ручной / пуск блок / пожар выкл).
 * LED_STOP — только пауза countdown / ПОЖАР·ОСТ.ПУСК (не режим зоны). */
static void rs_v3_sync_led_stop_status(void)
{
    uint8_t auto_off = 0u;
    uint8_t stop_on = 0u;
    uint8_t zi;
    uint8_t i;

    for (zi = 0u; zi < 32u; zi++) {
        uint8_t m;
        if (PanelHostCache_GetConst()->zone_name[zi][0] == 0) {
            continue;
        }
        m = PPKY_ZoneFireModeGet(zi);
        if (m == 2u || m == 3u || m == 4u) {
            auto_off = 1u;
            break;
        }
    }
    if (s_fire_active != 0u && s_zones_valid != 0u) {
        for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
            const uint8_t st = s_zones.items[i].status;
            if (st == (uint8_t)RS_PANEL_V3_ZONE_PAUSED ||
                st == (uint8_t)RS_PANEL_V3_ZONE_FIRE_STOPPED) {
                stop_on = 1u;
                break;
            }
        }
    }
    Led_Set(LED_AUTO_OFF, auto_off);
    if (auto_off != 0u) {
        Led_ForceStatusBright(LED_AUTO_OFF);
    }
    Led_Set(LED_STOP, stop_on);
    if (stop_on != 0u) {
        Led_ForceStatusBright(LED_STOP);
    }
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

    /* Не перебивать мигание ПУСК ОБЩИЙ / ПУСК СП на малой во время hold. */
    if ((s_hold_start_all != 0u && s_hold_start_all_committed == 0u) ||
        (s_hold_start_sp_cd != 0u && s_hold_start_sp_committed == 0u)) {
        return;
    }

    if (s_fire_active == 0u) {
        if (s_fire_snd_phase != RS_V3_FIRE_SND_IDLE) {
            Beeper_ContinuousOff();
            Beeper_FireAlarmOff();
            Beeper_StopPattern();
            Led_Set(LED_START, 0u);
            Led_Set(LED_BUT_START_SP, 0u);
            Led_Set(LED_STR_START_SP, 0u);
            Led_Set(LED_BUT_STOP, 0u);
            Led_Set(LED_STR_STOP, 0u);
            s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
            rs_v3_restore_fault_beep_if_needed();
        }
        rs_v3_sync_led_stop_status();
        return;
    }

    /* Без ZONES: тот же непрерывный тон, что и ПОЖАР2.
     * Раньше return → секунды тишины, пока ZONES режется ради ACK/стрима. */
    if (s_zones.count == 0u || s_zones_valid == 0u) {
        if (s_fire_snd_phase != RS_V3_FIRE_SND_ALERT) {
            s_fault_sound_phase = RS_V3_FS_IDLE;
            s_fault_sound_deadline_ms = 0u;
            Beeper_ContinuousOff();
            Beeper_FireAlarmOff();
            Beeper_StopPattern();
            Beeper_ResumeSoundOnNewEvent();
            Beeper_FireAlarmOn();
            s_fire_snd_phase = RS_V3_FIRE_SND_ALERT;
            Led_Set(LED_START, 0u);
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

    /* LED ПУСК СП / ОСТ.ПУСК — как Fire_ApplyStateLeds на ППКУ1. */
    {
        /* ПУСК СП доступен и в ПОЖАР1 (принудительный пуск). */
        uint8_t need_ctrl = 0u;
        for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
            const uint8_t st = s_zones.items[i].status;
            if (st == (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN ||
                st == (uint8_t)RS_PANEL_V3_ZONE_PAUSED ||
                st == (uint8_t)RS_PANEL_V3_ZONE_FIRE_STOPPED ||
                st == (uint8_t)RS_PANEL_V3_ZONE_EXT_ABORT ||
                st == (uint8_t)RS_PANEL_V3_ZONE_FIRE1) {
                need_ctrl = 1u;
                break;
            }
        }
        Led_Set(LED_BUT_START_SP, need_ctrl);
        Led_Set(LED_STR_START_SP, need_ctrl);
        Led_Set(LED_BUT_STOP, need_ctrl);
        Led_Set(LED_STR_STOP, need_ctrl);
        rs_v3_sync_led_stop_status();
    }

    fire1 = rs_v3_zones_want_fire1();
    /* ПОЖАР1 и ПОЖАР2 — один непрерывный ALERT: смену статуса не перезапускаем. */
    if (desired == s_fire_snd_phase) {
        Led_Set(LED_START, (desired == RS_V3_FIRE_SND_START) ? 1u : 0u);
        return;
    }

    s_fault_sound_phase = RS_V3_FS_IDLE;
    s_fault_sound_deadline_ms = 0u;
    Beeper_ContinuousOff();
    Beeper_FireAlarmOff();
    Beeper_StopPattern();

    if (desired == RS_V3_FIRE_SND_ALERT) {
        Beeper_ResumeSoundOnNewEvent();
        Beeper_FireAlarmOn();
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

/* UI-кэш: зоны и МКУ обновляем раздельно (не полный rebuild на каждый ADD). */
static void rs_v3_host_cache_put_zone(uint8_t zone, const char *name)
{
    PanelHostCache *host = PanelHostCache_Get();
    size_t n;

    if (zone == 0u || zone > PANEL_CATALOG_ZONE_SLOTS || host == 0) {
        return;
    }
    memset(host->zone_name[zone - 1u], 0, ZONE_NAME_SIZE);
    if (name == 0) {
        return;
    }
    n = strlen(name);
    if (n > ZONE_NAME_SIZE) {
        n = ZONE_NAME_SIZE;
    }
    if (n > 0u) {
        memcpy(host->zone_name[zone - 1u], name, n);
    }
}

static void rs_v3_host_cache_clear_zones(void)
{
    PanelHostCache *host = PanelHostCache_Get();
    if (host != 0) {
        memset(host->zone_name, 0, sizeof(host->zone_name));
    }
}

static void rs_v3_host_cache_put_device_at(uint16_t slot, const RsPanelV3DeviceItem *d)
{
    PanelHostCache *host = PanelHostCache_Get();
    if (host == 0 || d == 0 || slot >= MAX_MCU_IN_BUS) {
        return;
    }
    host->devices[slot].zone = d->zone;
    host->devices[slot].l_adr = d->l_adr;
    host->devices[slot].h_adr = d->h_adr;
    host->devices[slot].d_type = d->d_type;
    host->devices[slot].uid0 = d->uid0;
    host->devices[slot].uid1 = d->uid1;
    host->devices[slot].uid2 = d->uid2;
}

static void rs_v3_host_cache_clear_devices(void)
{
    PanelHostCache *host = PanelHostCache_Get();
    if (host != 0) {
        memset(host->devices, 0, sizeof(host->devices));
    }
}

static void rs_v3_sync_host_cache_from_catalog(void)
{
    uint8_t z;
    uint16_t i;

    rs_v3_host_cache_clear_zones();
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (s_zone_name_valid[z] != 0u) {
            rs_v3_host_cache_put_zone(z, s_zone_name_by_id[z]);
        }
    }
    rs_v3_host_cache_clear_devices();
    for (i = 0u; i < s_device_catalog_count && i < MAX_MCU_IN_BUS; i++) {
        rs_v3_host_cache_put_device_at(i, &s_device_catalog[i]);
    }
}

static void rs_v3_catalog_mark_flash_dirty(void)
{
    s_catalog_flash_dirty = 1u;
    s_catalog_flash_dirty_ms = HAL_GetTick();
}

static void rs_v3_catalog_flash_save_if_dirty(void)
{
    /* static: PanelCatalogBlob ~2KB, стек панели Min_Stack=4K. */
    static PanelCatalogBlob s_blob;
    uint8_t z;
    uint16_t i;

    if (s_catalog_flash_dirty == 0u) {
        return;
    }
    memset(&s_blob, 0, sizeof(s_blob));
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (s_zone_name_valid[z] == 0u) {
            continue;
        }
        s_blob.zone_valid[z - 1u] = 1u;
        memcpy(s_blob.zone_name[z - 1u], s_zone_name_by_id[z], RS_PANEL_V3_ZONE_NAME_LEN);
    }
    s_blob.device_count = s_device_catalog_count;
    for (i = 0u; i < s_device_catalog_count && i < RS_PANEL_V3_MAX_CATALOG_MCU; i++) {
        s_blob.devices[i] = s_device_catalog[i];
        s_blob.devices[i].op = (uint8_t)RS_PANEL_V3_STREAM_OP_ADD;
    }
    s_blob.zones_crc = s_zones_crc;
    s_blob.devices_crc = s_devices_crc;
    /* Снять dirty до erase: при сбое записи не крутить HardFault-цикл. */
    s_catalog_flash_dirty = 0u;
    PanelCatalog_FlashWrite(&s_blob);
}

static uint8_t rs_v3_catalog_flash_load(void)
{
    static PanelCatalogBlob s_blob;
    uint8_t z;
    uint16_t i;

    if (PanelCatalog_FlashRead(&s_blob) == 0u) {
        return 0u;
    }
    memset(s_zone_name_by_id, 0, sizeof(s_zone_name_by_id));
    memset(s_zone_name_valid, 0, sizeof(s_zone_name_valid));
    for (z = 1u; z <= PANEL_CATALOG_ZONE_SLOTS; z++) {
        if (s_blob.zone_valid[z - 1u] == 0u) {
            continue;
        }
        memcpy(s_zone_name_by_id[z], s_blob.zone_name[z - 1u], RS_PANEL_V3_ZONE_NAME_LEN);
        s_zone_name_by_id[z][RS_PANEL_V3_ZONE_NAME_LEN - 1u] = '\0';
        s_zone_name_valid[z] = 1u;
    }
    s_device_catalog_count = s_blob.device_count;
    if (s_device_catalog_count > RS_PANEL_V3_MAX_CATALOG_MCU) {
        s_device_catalog_count = RS_PANEL_V3_MAX_CATALOG_MCU;
    }
    memset(s_device_catalog, 0, sizeof(s_device_catalog));
    for (i = 0u; i < s_device_catalog_count; i++) {
        s_device_catalog[i] = s_blob.devices[i];
    }
    rs_v3_recalc_catalog_crc();
    /* CRC в blob — контроль целостности; при расхождении считаем Flash битым. */
    if (s_blob.zones_crc != s_zones_crc || s_blob.devices_crc != s_devices_crc) {
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
    switch (v_d_type & 0x7Fu) {
    case DEVICE_DPT_TYPE: return "ДПТ";
    case DEVICE_IGNITER_TYPE: return "СП";
    case DEVICE_BUTTON_TYPE: return "КН";
    case DEVICE_LSWITCH_TYPE: return "КОН";
    default: return 0;
    }
}

/* Как ППКУ1 Fire_FormatZoneUiName: каталог зоны + « МКУ N ДПТk». */
static void rs_v3_format_fire_zone_line(const RsPanelV3ZoneItem *z, char *out, uint16_t out_sz)
{
    char zone_name[RS_PANEL_V3_ZONE_NAME_LEN];
    const char *ch_s;

    if (out == 0 || out_sz == 0u) {
        return;
    }
    out[0] = '\0';
    if (z == 0) {
        return;
    }
    rs_v3_zone_name(z->zone, zone_name, (uint16_t)sizeof(zone_name));
    ch_s = (z->ch_type != 0u) ? rs_v3_ch_type_short(z->ch_type) : 0;
    if (ch_s != 0 && z->mcu_h_adr != 0u) {
        (void)snprintf(out, out_sz, "%s МКУ %u %s%u",
                       zone_name, (unsigned)z->mcu_h_adr, ch_s,
                       (unsigned)z->ch_l_adr);
    } else if (ch_s != 0) {
        (void)snprintf(out, out_sz, "%s %s%u",
                       zone_name, ch_s, (unsigned)z->ch_l_adr);
    } else if (z->mcu_h_adr != 0u) {
        (void)snprintf(out, out_sz, "%s МКУ %u",
                       zone_name, (unsigned)z->mcu_h_adr);
    } else {
        (void)snprintf(out, out_sz, "%s", zone_name);
    }
    out[out_sz - 1u] = '\0';
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
    case RS_PANEL_V3_FAULT_PPKU_POWER_IN2: {
        /* extra: 1=ОБРЫВ, 2=НИЖЕ, 3=ВЫШЕ (как PPKU_INPUT_FAULT_* на ППКУ). */
        const char *kind_txt = "ОБРЫВ";
        if ((fe->flags & RS_PANEL_V3_FAULT_FLAG_HAS_EXTRA) != 0u) {
            if (fe->extra == 2) {
                kind_txt = "НИЖЕ";
            } else if (fe->extra == 3) {
                kind_txt = "ВЫШЕ";
            }
        }
        (void)snprintf(title, title_sz, "ПИТ%u. %s",
                       (unsigned)(fe->code - RS_PANEL_V3_FAULT_PPKU_POWER_IN1 + 1u),
                       kind_txt);
        rs_v3_format_ppku_sn(fe, detail, detail_sz);
        break;
    }
    case RS_PANEL_V3_FAULT_LINE_BREAK:
    case RS_PANEL_V3_FAULT_LINE_SHORT:
    case RS_PANEL_V3_FAULT_LINE_OTHER: {
        const char *fault = "ОБРЫВ";
        if (fe->code == RS_PANEL_V3_FAULT_LINE_SHORT) {
            fault = "КЗ";
        } else if (fe->code == RS_PANEL_V3_FAULT_LINE_OTHER) {
            fault = "НЕИСП";
        }
        {
            const char *ch_s = rs_v3_ch_type_short(fe->ch_type);
            (void)snprintf(title, title_sz, "%s %s%u", fault,
                           (ch_s != 0) ? ch_s : "?", (unsigned)fe->ch_l_adr);
        }
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
        rs_v3_host_cache_clear_zones();
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
    rs_v3_host_cache_put_zone(zn->zone, s_zone_name_by_id[zn->zone]);
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
        rs_v3_host_cache_clear_devices();
        rs_v3_catalog_mark_flash_dirty();
        return;
    }
    if (s_device_catalog_count < RS_PANEL_V3_MAX_CATALOG_MCU) {
        const uint16_t slot = s_device_catalog_count;
        s_device_catalog[slot] = *dev;
        s_device_catalog_count++;
        rs_v3_recalc_catalog_crc();
        rs_v3_host_cache_put_device_at(slot, dev);
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
        /* Конец фазы faults / каталога: READY, CRC зон/МКУ уже актуальны. */
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        rs_v3_recalc_catalog_crc();
        rs_v3_sync_host_cache_from_catalog();
        /* Не пушить UI сразу: держим прежний экран до пакета SET (quiet-timer).
         * Иначе АВАРИЯ n/n скачет 0→placeholder→1→2… */
        if (s_has_faults == 0u) {
            Led_Set(LED_ERR, 0u);
            rs_v3_push_faults_ui();
            Beeper_StopPattern();
            s_fault_sound_phase = RS_V3_FS_IDLE;
            s_fault_sound_deadline_ms = 0u;
            s_fault_sound_resync = 0u;
            s_fault_pre_clear_count = 0u;
            s_fault_clear_safe_ms = 0u;
        } else {
            /* Resync: НЕ гасить LED_ERR / не зажигать LED_NORM — иначе миг «НОРМА»
             * между CLEAR_ALL и SET при уходе одной из нескольких неисправностей. */
            s_fault_sound_resync = 1u;
            s_fault_resync_wait_ms = HAL_GetTick();
            s_fault_clear_safe_ms = 0u;
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
             * на каждый повторный SET старых неисправностей.
             * При активном ПОЖАРЕ/тушении SIGNAL глушим; после тушения (DUTY) — играем. */
            if (s_fault_sound_resync == 0u &&
                rs_v3_allow_fault_signal_now() != 0u) {
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
    const uint8_t from_host = (z != 0 && z != &s_zones) ? 1u : 0u;
    if (z == 0) {
        return;
    }
    /* Тот же снимок, что уже в s_zones — не format/UI/sync.
     * При пожаре POLL ~40 мс, remaining_s меняется раз в секунду. */
    if (from_host != 0u && s_zones_valid != 0u) {
        uint8_t n = z->count;
        if (n > RS_PANEL_V3_MAX_ZONES) {
            n = RS_PANEL_V3_MAX_ZONES;
        }
        if (s_zones.count == z->count &&
            (n == 0u ||
             memcmp(s_zones.items, z->items,
                    (size_t)n * sizeof(RsPanelV3ZoneItem)) == 0)) {
            return;
        }
    }
    /* Статусы/таймер на экране — только снимок хоста (без optimistic). */
    if (from_host != 0u) {
        s_zones_apply_gen++;
        if (s_zones_apply_gen == 0u) {
            s_zones_apply_gen = 1u;
        }
        s_zones = *z;
    } else if (s_zones_valid == 0u) {
        s_zones = *z;
    }
    s_zones_valid = 1u;
    /* 1 зона → всегда она; 2+ → листание или 0 (все); выбор невалиден → 0. */
    if (s_zones.count == 0u) {
        s_selected_zone = 0u;
    } else if (s_zones.count == 1u) {
        s_selected_zone = s_zones.items[0].zone;
    } else if (s_selected_zone != 0u) {
        uint8_t found = 0u;
        uint8_t j;
        for (j = 0u; j < s_zones.count && j < RS_PANEL_V3_MAX_ZONES; j++) {
            if (s_zones.items[j].zone == s_selected_zone) {
                found = 1u;
                break;
            }
        }
        if (found == 0u) {
            s_selected_zone = 0u;
        }
    }
    for (i = 0u; i < s_zones.count && i < RS_PANEL_V3_MAX_ZONES; i++) {
        if (s_zones.items[i].status ==
            (uint8_t)RS_PANEL_V3_ZONE_EXTINGUISHING) {
            s_local_timer_s[i] = 0u;
        } else if (from_host != 0u &&
                   (s_zones.items[i].status ==
                        (uint8_t)RS_PANEL_V3_ZONE_COUNTDOWN ||
                    s_zones.items[i].status ==
                        (uint8_t)RS_PANEL_V3_ZONE_PAUSED)) {
            /* Таймер экрана = remaining хоста (при PAUSED хост не уменьшает). */
            s_local_timer_s[i] = s_zones.items[i].remaining_s;
        }
    }
    /* Push into existing UI bridge as MAIN_FIRE-like snapshot. */
    {
        char names[16][ZONE_NAME_SIZE + 1];
        uint8_t modes[16];
        uint8_t rems[16];
        uint8_t n = s_zones.count;
        /* Без SYS_FIRE_ACTIVE не держать «ДО ПУСКА» после сброса на ППКУ. */
        uint8_t active = (s_fire_active != 0u && n > 0u) ? 1u : 0u;
        uint8_t mode = 0u;
        uint8_t rem = 0u;
        if (n > 16u) {
            n = 16u;
        }
        memset(names, 0, sizeof(names));
        for (i = 0u; i < n; i++) {
            /* Имя зоны из каталога; МКУ/канал — из ZONES snapshot. */
            rs_v3_format_fire_zone_line(&s_zones.items[i], names[i],
                                       (uint16_t)sizeof(names[i]));
            modes[i] = s_zones.items[i].status;
            rems[i] = s_local_timer_s[i];
            if (i == 0u) {
                mode = modes[i];
                rem = rems[i];
            }
        }
        /* Не SetFireStatus из UART IRQ: при пожаре ZONES в каждом POLL,
         * TouchGFX в OnPoll срывает turnaround → START_SP до Fire_OnPanelStartSp
         * пропускается или приходит через секунды. Очередь → ProcessDeferredUi. */
        RsPanelEndpoint_QueueFireUi(active, mode, rem, n, names, modes, rems);
    }
    rs_v3_sync_fire_indication();
}

static uint8_t rs_v3_poll_decode(const uint8_t *payload, uint16_t len,
                                 RsPanelV3Poll *out)
{
    if (payload == 0 || out == 0 || len < 1u || payload[0] != RS_PANEL_V3_VERSION) {
        return 0u;
    }
    if (RsPanelV3_DecodePoll(payload, len, out) == 0u) {
        return 0u;
    }
    return 1u;
}

static void rs_v3_mark_v3_active(void)
{
    if (s_v3_active == 0u) {
        s_v3_active = 1u;
        /* После Led_Init / перехода в v3 — снова дежурная надпись ПУСК ОБЩИЙ. */
        if (s_hold_start_all == 0u) {
            rs_v3_restore_start_all_idle_leds();
        }
    }
}

/* Только TAG_ACK из POLL — без DecodePoll/RsPanelV3Poll на стеке IRQ.
 * ZONES/TIME/… разбирает OnPollBody в main. */
static uint8_t rs_v3_poll_extract_acks(const uint8_t *payload, uint16_t len,
                                       RsPanelV3Ack *out_ack,
                                       RsPanelV3Ack *out_ack_crit)
{
    uint16_t pos = 0u;
    uint8_t ack_n = 0u;
    if (payload == 0 || len < 1u || payload[0] != RS_PANEL_V3_VERSION) {
        return 0u;
    }
    if (out_ack != 0) {
        memset(out_ack, 0, sizeof(*out_ack));
    }
    if (out_ack_crit != 0) {
        memset(out_ack_crit, 0, sizeof(*out_ack_crit));
    }
    pos = 1u;
    while (pos < len) {
        uint8_t tag;
        uint16_t body_len;
        uint16_t body_start;
        uint16_t body_end;
        tag = payload[pos++];
        if (tag == (uint8_t)RS_PANEL_V3_TAG_ZONES ||
            tag == (uint8_t)RS_PANEL_V3_TAG_EVENT_REPLY) {
            if ((uint16_t)(pos + 2u) > len) {
                return 0u;
            }
            body_len = (uint16_t)payload[pos] | ((uint16_t)payload[pos + 1u] << 8);
            pos = (uint16_t)(pos + 2u);
        } else {
            if (pos >= len) {
                return 0u;
            }
            body_len = payload[pos++];
        }
        body_start = pos;
        body_end = (uint16_t)(body_start + body_len);
        if (body_end > len) {
            return 0u;
        }
        if (tag == (uint8_t)RS_PANEL_V3_TAG_ACK && body_len >= 2u) {
            if (ack_n == 0u && out_ack != 0) {
                out_ack->seq = payload[body_start];
                out_ack->result = payload[body_start + 1u];
                ack_n = 1u;
            } else if (ack_n == 1u && out_ack_crit != 0) {
                out_ack_crit->seq = payload[body_start];
                out_ack_crit->result = payload[body_start + 1u];
                ack_n = 2u;
            }
        }
        pos = body_end;
    }
    return 1u;
}

static void rs_v3_apply_poll_acks_raw(RsPanelV3Ack *ack, RsPanelV3Ack *ack_crit)
{
    if (ack == 0 || ack_crit == 0) {
        return;
    }
    /* Compat: один TAG_ACK (только critical на проводе) старый encode клал
     * в ack. Если seq совпал с critical и не с ordinary — это crit ACK. */
    if (ack_crit->result == (uint8_t)RS_PANEL_V3_ACK_NONE &&
        ack->result != (uint8_t)RS_PANEL_V3_ACK_NONE &&
        ack->seq != 0u &&
        s_event_crit.seq != 0u &&
        ack->seq == s_event_crit.seq &&
        ack->seq != s_event.seq) {
        *ack_crit = *ack;
        ack->seq = 0u;
        ack->result = (uint8_t)RS_PANEL_V3_ACK_NONE;
    }

    /* Ordinary ACK — только ordinary lane (журнал/меню/wifi/…). */
    if (ack->result != (uint8_t)RS_PANEL_V3_ACK_NONE &&
        ack->seq != 0u && ack->seq == s_event.seq) {
        rs_v3_evt_clear_inflight();
        rs_v3_evt_retry_sticky();
        rs_v3_evt_flush_queue();
        rs_v3_evt_flush_crit();
    }

    /* Critical ACK — fire-ctrl (START_SP/STOP/START_ALL/FIRE_RESET). */
    if (ack_crit->result != (uint8_t)RS_PANEL_V3_ACK_NONE &&
        ack_crit->seq != 0u && ack_crit->seq == s_event_crit.seq) {
        const uint8_t acked_type = s_event_crit.type;
        const uint8_t acked_seq = ack_crit->seq;
        rs_v3_evt_clear_inflight_crit();
        if (acked_type == (uint8_t)RS_PANEL_V3_EVT_START_ALL_COMMIT) {
            /* ACK = ППКУ принял команду; тушить/нет решает хост. */
            s_start_all_evt_pending = 0u;
            /* >>> RS_V3_DBG_START_ALL_LAG BEGIN */
            {
                const uint32_t now = HAL_GetTick();
                g_rs_v3_dbg_sa.ack_ms = now;
                g_rs_v3_dbg_sa.ack_seq = acked_seq;
                if (g_rs_v3_dbg_sa.post_ms != 0u) {
                    g_rs_v3_dbg_sa.dt_post_to_ack_ms = now - g_rs_v3_dbg_sa.post_ms;
                }
                if (g_rs_v3_dbg_sa.first_build_ms != 0u) {
                    g_rs_v3_dbg_sa.dt_build_to_ack_ms =
                        now - g_rs_v3_dbg_sa.first_build_ms;
                }
                rs_v3_dbg_sa_mark(3u);
            }
            /* >>> RS_V3_DBG_START_ALL_LAG END */
        } else if (acked_type == (uint8_t)RS_PANEL_V3_EVT_START_SP) {
            s_start_sp_evt_pending = 0u;
        } else if (acked_type == (uint8_t)RS_PANEL_V3_EVT_STOP_LAUNCH) {
            s_stop_launch_evt_pending = 0u;
        } else if (acked_type == (uint8_t)RS_PANEL_V3_EVT_FIRE_RESET) {
            s_fire_reset_evt_pending = 0u;
            rs_v3_cancel_local_holds();
            rs_v3_push_fire_ui_clear();
        }
        rs_v3_evt_retry_sticky();
        rs_v3_evt_flush_queue();
        rs_v3_evt_flush_crit();
    }
}

static void rs_v3_apply_poll_body(const RsPanelV3Poll *poll)
{
    if (poll == 0) {
        return;
    }
    {
        const uint8_t prev_ready = s_sys_ready;
        const uint8_t prev_cfg = s_config_active;
        const uint8_t prev_fire = s_fire_active;
        /* addr_scan probe: только SYS с flags=0, без TIME/каталога — не сбрасывать
         * SYS_READY (иначе «ПРОВЕРКА» навечно, пока мастер не сделал ADD). */
        const uint8_t scan_probe =
            (poll->sys.flags == 0u &&
             poll->has_time == 0u &&
             poll->has_zones == 0u &&
             poll->has_zone_names == 0u &&
             poll->has_devices == 0u &&
             poll->has_fault_evt == 0u &&
             poll->has_fault_op == 0u &&
             poll->has_config == 0u &&
             poll->has_event_reply == 0u) ? 1u : 0u;

        if (scan_probe == 0u) {
            s_sys_ready = ((poll->sys.flags & RS_PANEL_V3_SYS_READY) != 0u) ? 1u : 0u;
            s_fire_active =
                ((poll->sys.flags & RS_PANEL_V3_SYS_FIRE_ACTIVE) != 0u) ? 1u : 0u;
            s_config_active =
                ((poll->sys.flags & RS_PANEL_V3_SYS_CONFIG_ACTIVE) != 0u) ? 1u : 0u;
            s_has_faults =
                ((poll->sys.flags & RS_PANEL_V3_SYS_HAS_FAULTS) != 0u) ? 1u : 0u;
            s_power_input_fault =
                ((poll->sys.flags & RS_PANEL_V3_SYS_POWER_INPUT_FAULT) != 0u) ? 1u : 0u;

            if (prev_fire != s_fire_active) {
                if (s_fire_active != 0u) {
                    s_fire_snd_phase = RS_V3_FIRE_SND_IDLE;
                } else {
                    /* Сброс пожара: снять hold-таймер и «ДО ПУСКА» сразу. */
                    rs_v3_cancel_local_holds();
                    rs_v3_push_fire_ui_clear();
                }
                rs_v3_sync_fire_indication();
            }

            /* ППКУ — источник истины для mute / beep_block (в т.ч. откат при ACK_DENIED). */
            {
                const uint8_t prev_beep = PanelHostCache_GetConst()->beep;
                const uint8_t sound_on =
                    ((poll->sys.flags & RS_PANEL_V3_SYS_SOUND_ON) != 0u) ? 1u : 0u;
                const uint8_t sound_blk =
                    ((poll->sys.flags & RS_PANEL_V3_SYS_SOUND_BLOCKED) != 0u) ? 1u : 0u;
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
                    ((poll->sys.flags & RS_PANEL_V3_SYS_WIFI_ON) != 0u) ? 1u : 0u;
                const uint8_t wifi_link =
                    ((poll->sys.flags & RS_PANEL_V3_SYS_WIFI_LINK) != 0u) ? 1u : 0u;
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
            if (prev_cfg != 0u && s_config_active == 0u && poll->has_config == 0u) {
                MenuConfig_SetRemoteStatus(MENU_CFG_STATE_IDLE, 0u);
            }
        }
    }

    if (s_fire_active != 0u || s_config_active != 0u) {
        MenuUi_SetMainScreenActive(1u);
    }

    if (poll->has_time != 0u) {
        RTC_TimeTypeDef time = {0};
        RTC_DateTypeDef date = {0};
        time.Hours = poll->time.hour;
        time.Minutes = poll->time.min;
        time.Seconds = poll->time.sec;
        date.Date = poll->time.day;
        date.Month = poll->time.month;
        date.Year = poll->time.year;
        date.WeekDay = RTC_WEEKDAY_MONDAY;
        (void)HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
        (void)HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);
        /* UI читает RtcCache (Model::tick), не сырой RTC — иначе до Tick1s 00:00. */
        RtcCache_Refresh();
    }

    /* ZONE_NAMES/DEVICES/FAULT_EVT — OnPollStream в UART IRQ (не здесь):
     * иначе повторный ADD и потеря кадров в одном deferred-слоте. */
    if (poll->has_zones != 0u) {
        rs_v3_apply_zones(&poll->zones);
    }

    if (poll->has_event_reply != 0u) {
        const RsPanelV3EventReply *er = &poll->event_reply;
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
        } else if (er->type == (uint8_t)RS_PANEL_V3_EVT_ZONE_BLOCK_LIST &&
                   er->payload_len >= 1u) {
            /* payload: n, [zone1based, mode]* — имена зон уже из ZONE_NAMES. */
            const uint8_t n = er->payload[0];
            uint8_t i;
            PanelHostCache *host = PanelHostCache_Get();
            for (i = 0u; i < n; i++) {
                uint16_t off = (uint16_t)(1u + (uint16_t)i * 2u);
                uint8_t zone1;
                uint8_t mode;
                if ((uint16_t)(off + 1u) >= er->payload_len) {
                    break;
                }
                zone1 = er->payload[off];
                mode = er->payload[off + 1u];
                if (zone1 >= 1u && zone1 <= 32u) {
                    host->zone_fire_mode[zone1 - 1u] = (uint8_t)(mode & 0x07u);
                }
            }
            rs_v3_sync_led_stop_status();
        } else if (er->type == (uint8_t)RS_PANEL_V3_EVT_ZONE_BLOCK_SET &&
                   er->payload_len >= 2u) {
            uint8_t zone1 = er->payload[0];
            uint8_t mode = er->payload[1];
            if (zone1 >= 1u && zone1 <= 32u) {
                PanelHostCache_Get()->zone_fire_mode[zone1 - 1u] =
                    (uint8_t)(mode & 0x07u);
            }
            rs_v3_sync_led_stop_status();
        }
        /* Prefetch журнала — только ordinary idle; critical может быть занят. */
        if (s_fire_active == 0u &&
            s_event.seq == 0u && s_evt_q_n == 0u) {
            PanelJournalCache_PrefetchProcess();
        }
    }

    if (poll->has_config != 0u) {
        if (poll->config.phase == (uint8_t)RS_PANEL_V3_CFG_SUCCESS) {
            Beeper_StartPulseTrain(100u, 100u, 3u, 0u);
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_SUCCESS, 100u);
        } else if (poll->config.phase == (uint8_t)RS_PANEL_V3_CFG_FAIL) {
            Beeper_StartPulseTrain(SOUND_FAULT_DUTY_ON_MS, SOUND_FAULT_DUTY_OFF_MS,
                                   SOUND_FAULT_DUTY_PULSES, 0u);
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_IDLE, poll->config.percent);
        } else if (poll->config.phase == (uint8_t)RS_PANEL_V3_CFG_RUNNING) {
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_RECEIVING, poll->config.percent);
        } else {
            MenuConfig_SetRemoteStatus(MENU_CFG_STATE_IDLE, 0u);
        }
    }

    if (s_sys_ready != 0u && s_fault_ui_dirty == 0u) {
        if (s_has_faults != 0u && s_active_fault_count == 0u) {
            /* Ждём FAULT_SET — без placeholder «НЕИСП». */
            s_fault_clear_safe_ms = 0u;
            rs_v3_wait_fault_evt_ui();
        } else if (s_has_faults == 0u &&
                   s_fault_sound_resync == 0u &&
                   (s_active_fault_count > 0u ||
                    s_fault_placeholder_active != 0u ||
                    s_fault_ui_pushed_count > 0u)) {
            /* HAS_FAULTS=0, а список/UI ещё живы. Debounce: один POLL с флагом 0
             * при CLEAR_ALL→SET давал миг «НОРМА» при оставшихся неисправностях. */
            const uint32_t now_safe = HAL_GetTick();
            if (s_fault_clear_safe_ms == 0u) {
                s_fault_clear_safe_ms = now_safe;
            } else if ((now_safe - s_fault_clear_safe_ms) >= RS_V3_FAULT_CLEAR_SAFE_MS) {
                if (s_active_fault_count > 0u) {
                    s_active_fault_count = 0u;
                    memset(s_active_faults, 0, sizeof(s_active_faults));
                    rs_v3_recalc_catalog_crc();
                    Led_Set(LED_ERR, 0u);
                }
                s_fault_placeholder_active = 0u;
                s_fault_sound_resync = 0u;
                s_fault_pre_clear_count = 0u;
                s_fault_clear_safe_ms = 0u;
                Beeper_StopPattern();
                s_fault_sound_phase = RS_V3_FS_IDLE;
                s_fault_sound_deadline_ms = 0u;
                rs_v3_push_faults_ui();
            }
        } else {
            s_fault_clear_safe_ms = 0u;
        }
    }
}

uint8_t RsPanelV3Slave_OnPollAcks(const uint8_t *payload, uint16_t len)
{
    RsPanelV3Ack ack;
    RsPanelV3Ack ack_crit;
    /* Не DecodePoll: RsPanelV3Poll на стеке UART IRQ при ZONES раздувает стек
     * и при пожаре ломает доставку event_crit на мастер. */
    if (rs_v3_poll_extract_acks(payload, len, &ack, &ack_crit) == 0u) {
        return 0u;
    }
    rs_v3_mark_v3_active();
    rs_v3_apply_poll_acks_raw(&ack, &ack_crit);
    return 1u;
}

/* Каталог/FAULT из POLL в UART IRQ — без RsPanelV3Poll (~большой стек) и без
 * deferred-очереди (CLEAR/ADD/FAULT_SET больше не затираются). */
uint8_t RsPanelV3Slave_OnPollStream(const uint8_t *payload, uint16_t len)
{
    uint16_t pos = 0u;
    if (payload == 0 || len < 1u || payload[0] != RS_PANEL_V3_VERSION) {
        return 0u;
    }
    rs_v3_mark_v3_active();
    pos = 1u;
    while (pos < len) {
        uint8_t tag;
        uint16_t body_len;
        uint16_t body_start;
        uint16_t body_end;
        tag = payload[pos++];
        if (tag == (uint8_t)RS_PANEL_V3_TAG_ZONES ||
            tag == (uint8_t)RS_PANEL_V3_TAG_EVENT_REPLY) {
            if ((uint16_t)(pos + 2u) > len) {
                return 0u;
            }
            body_len = (uint16_t)payload[pos] | ((uint16_t)payload[pos + 1u] << 8);
            pos = (uint16_t)(pos + 2u);
        } else {
            if (pos >= len) {
                return 0u;
            }
            body_len = payload[pos++];
        }
        body_start = pos;
        body_end = (uint16_t)(body_start + body_len);
        if (body_end > len) {
            return 0u;
        }

        if (tag == (uint8_t)RS_PANEL_V3_TAG_ZONE_NAMES && body_len >= 1u) {
            RsPanelV3ZoneNameItem zn;
            uint16_t zp = body_start;
            uint8_t op = payload[zp++];
            memset(&zn, 0, sizeof(zn));
            zn.op = op;
            if (op == (uint8_t)RS_PANEL_V3_STREAM_OP_ADD &&
                (uint16_t)(zp + 2u) <= body_end) {
                uint8_t name_len;
                zn.zone = payload[zp++];
                name_len = payload[zp++];
                if (name_len > 0u && (uint16_t)(zp + name_len) <= body_end) {
                    uint16_t copy = name_len;
                    if (copy >= RS_PANEL_V3_ZONE_NAME_LEN) {
                        copy = (uint16_t)(RS_PANEL_V3_ZONE_NAME_LEN - 1u);
                    }
                    memcpy(zn.name, &payload[zp], copy);
                    zn.name[copy] = '\0';
                }
            }
            rs_v3_apply_zone_names(&zn);
        } else if (tag == (uint8_t)RS_PANEL_V3_TAG_DEVICES && body_len >= 1u) {
            RsPanelV3DeviceItem d;
            uint16_t dp = body_start;
            uint8_t i;
            memset(&d, 0, sizeof(d));
            d.op = payload[dp++];
            if (d.op == (uint8_t)RS_PANEL_V3_STREAM_OP_ADD) {
                /* После op: d_type,h,l,zone (4) + uid0..2 (12) + n_ch (1) = 17.
                 * Каналы: n_ch × 2. Неполный TLV — не apply (без «затычек»). */
                if ((uint16_t)(dp + 17u) > body_end) {
                    pos = body_end;
                    continue;
                }
                d.d_type = payload[dp++];
                d.h_adr = payload[dp++];
                d.l_adr = payload[dp++];
                d.zone = payload[dp++];
                d.uid0 = (uint32_t)payload[dp] |
                         ((uint32_t)payload[dp + 1u] << 8) |
                         ((uint32_t)payload[dp + 2u] << 16) |
                         ((uint32_t)payload[dp + 3u] << 24);
                dp = (uint16_t)(dp + 4u);
                d.uid1 = (uint32_t)payload[dp] |
                         ((uint32_t)payload[dp + 1u] << 8) |
                         ((uint32_t)payload[dp + 2u] << 16) |
                         ((uint32_t)payload[dp + 3u] << 24);
                dp = (uint16_t)(dp + 4u);
                d.uid2 = (uint32_t)payload[dp] |
                         ((uint32_t)payload[dp + 1u] << 8) |
                         ((uint32_t)payload[dp + 2u] << 16) |
                         ((uint32_t)payload[dp + 3u] << 24);
                dp = (uint16_t)(dp + 4u);
                d.n_ch = payload[dp++];
                if (d.n_ch > RS_PANEL_V3_MAX_MCU_CHANNELS) {
                    d.n_ch = RS_PANEL_V3_MAX_MCU_CHANNELS;
                }
                if ((uint16_t)(dp + (uint16_t)d.n_ch * 2u) > body_end) {
                    pos = body_end;
                    continue;
                }
                for (i = 0u; i < d.n_ch; i++) {
                    d.ch[i].ch_type = payload[dp++];
                    d.ch[i].ch_l_adr = payload[dp++];
                }
            }
            rs_v3_apply_devices(&d);
        } else if (tag == (uint8_t)RS_PANEL_V3_TAG_FAULT_EVT && body_len >= 1u) {
            RsPanelV3FaultEvtItem fe;
            uint16_t fp = body_start;
            uint8_t op = payload[fp++];
            memset(&fe, 0, sizeof(fe));
            fe.op = op;
            if (op != (uint8_t)RS_PANEL_V3_FAULT_EVT_CLEAR_ALL) {
                uint16_t blen = (uint16_t)(body_end - fp);
                if (RsPanelV3_FaultEvtDecodeBody(&payload[fp], blen, &fe) == 0u) {
                    pos = body_end;
                    continue;
                }
                fe.op = op;
            }
            rs_v3_apply_fault_evt(&fe);
        }
        pos = body_end;
    }
    return 1u;
}

uint8_t RsPanelV3Slave_OnPollBody(const uint8_t *payload, uint16_t len)
{
    RsPanelV3Poll poll;
    if (rs_v3_poll_decode(payload, len, &poll) == 0u) {
        return 0u;
    }
    rs_v3_mark_v3_active();
    /* Каталог уже в OnPollStream — не apply повторно. */
    poll.has_zone_names = 0u;
    poll.has_devices = 0u;
    poll.has_fault_evt = 0u;
    rs_v3_apply_poll_body(&poll);
    return 1u;
}

uint16_t RsPanelV3Slave_BuildRsp(uint8_t *dst, uint16_t dst_size)
{
    RsPanelV3Rsp rsp;
    rs_v3_evt_retry_sticky();
    rs_v3_evt_flush_queue();
    rs_v3_evt_flush_crit();
    memset(&rsp, 0, sizeof(rsp));
    rsp.event = s_event;
    rsp.event_crit = s_event_crit;
    /* Ordinary пуст — продублировать fire-ctrl в event (первый слот RSP).
     * Иначе при любом сбое decode event_crit мастер не видит START_SP. */
    if (s_event.seq == 0u && s_event_crit.seq != 0u) {
        rsp.event = s_event_crit;
    }
    rsp.zones_crc = s_zones_crc;
    rsp.devices_crc = s_devices_crc;
    rsp.faults_crc = s_faults_crc;
    rsp.flags = s_rsp_flags;
    /* >>> RS_V3_DBG_START_ALL_LAG BEGIN */
    if (s_event_crit.type == (uint8_t)RS_PANEL_V3_EVT_START_ALL_COMMIT &&
        s_event_crit.seq != 0u && g_rs_v3_dbg_sa.post_ms != 0u) {
        const uint32_t now = HAL_GetTick();
        g_rs_v3_dbg_sa.build_hits++;
        if (g_rs_v3_dbg_sa.first_build_ms == 0u) {
            g_rs_v3_dbg_sa.first_build_ms = now;
            g_rs_v3_dbg_sa.first_build_seq = s_event_crit.seq;
            g_rs_v3_dbg_sa.dt_post_to_build_ms = now - g_rs_v3_dbg_sa.post_ms;
            rs_v3_dbg_sa_mark(2u);
        }
    }
    /* >>> RS_V3_DBG_START_ALL_LAG END */
    return RsPanelV3_EncodeRsp(dst, dst_size, &rsp);
}

void RsPanelV3Slave_OnButtonSample(void)
{
    ButtonState st_all = Button_GetState(BUT_FORCE);
    ButtonState st_stop = Button_GetState(BUT_STOP);
    ButtonState st_fire = Button_GetState(BUT_FIRE);
    uint8_t all_down = (st_all == ButtonStatePress || st_all == ButtonStateLongPress) ? 1u : 0u;
    uint8_t stop_down = (st_stop == ButtonStatePress || st_stop == ButtonStateLongPress) ? 1u : 0u;
    /* Только антидребезг Button stack (SHORT_PRESS), без сырого GPIO. */
    uint8_t fire_down = (st_fire == ButtonStatePress || st_fire == ButtonStateLongPress) ? 1u : 0u;
    uint8_t is_small = PANEL_TYPE_IS_SMALL(PanelCfg_Get()->panel_type) ? 1u : 0u;
    uint32_t now = HAL_GetTick();

    rs_v3_evt_inflight_watchdog();

    /* ——— ОСТАНОВ ———
     * Пауза/resume: только RELEASE, если не додержали до сброса.
     * Сброс пожара: удержание ≥ RS_V3_HOLD_FIRE_RESET_MS при активном пожаре. */
    if (stop_down != 0u) {
        if (s_btn_stop_evt_down == 0u) {
            s_stop_press_ms = 0u;
            s_hold_reset = 0u;
            s_hold_reset_ms = 0u;
            s_hold_reset_committed = 0u;
        } else if (s_stop_press_ms < 0xFFF0u) {
            s_stop_press_ms = (uint16_t)(s_stop_press_ms + 10u);
        }
        if (s_fire_active != 0u) {
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
                s_fire_reset_evt_zone = s_selected_zone;
                rs_v3_cancel_local_holds();
                rs_v3_force_fire_event((uint8_t)RS_PANEL_V3_EVT_FIRE_RESET,
                                       s_fire_reset_evt_zone);
                s_fire_reset_evt_pending = 0u;
            }
        }
    } else if (s_btn_stop_evt_down != 0u) {
        /* RELEASE: пауза/resume, если не был commit RESET. */
        if (s_hold_reset_committed == 0u) {
            rs_v3_post_stop_launch();
        }
        s_hold_reset = 0u;
        s_hold_reset_ms = 0u;
        s_hold_reset_committed = 0u;
        s_stop_press_ms = 0u;
    }
    s_btn_stop_evt_down = stop_down;

    /* ——— ПУСК СП ———
     * Малая: с нажатия UI-таймер 3с (как ПУСК ОБЩИЙ) → START_ALL_COMMIT;
     *        отпуск раньше commit → START_SP (пуск зоны).
     * Большая: RELEASE <2с → START_SP; hold ≥2с → без EVT. */
    if (is_small != 0u) {
        if (fire_down != 0u) {
            if (s_hold_start_sp == 0u) {
                s_hold_start_sp = 1u;
                s_hold_start_sp_ms = 0u;
                s_hold_start_sp_cd = 1u;
                s_hold_start_sp_cd_ms = 0u;
                s_hold_start_sp_committed = 0u;
                s_blink_on = 1u;
                s_blink_last_ms = now;
                Led_SetBrightness(LED_BUT_START_SP, LED_BUT_MAX_BRIGHTNESS);
                Led_SetBrightness(LED_STR_START_SP, LED_BUT_MAX_BRIGHTNESS);
                Led_Set(LED_BUT_START_SP, 1u);
                Led_Set(LED_STR_START_SP, 1u);
                rs_v3_start_hold_beep();
            } else if (s_hold_start_sp_ms < 0xFFF0u) {
                s_hold_start_sp_ms = (uint16_t)(s_hold_start_sp_ms + 10u);
            }
            if (s_hold_start_sp_cd != 0u && s_hold_start_sp_committed == 0u) {
                if (s_hold_start_sp_cd_ms < RS_V3_SP_COUNTDOWN_MS) {
                    s_hold_start_sp_cd_ms = (uint16_t)(s_hold_start_sp_cd_ms + 10u);
                }
                if (s_hold_start_sp_cd_ms >= RS_V3_SP_COUNTDOWN_MS) {
                    s_hold_start_sp_committed = 1u;
                    Beeper_StopPattern();
                    rs_v3_restore_fault_beep_if_needed();
                    rs_v3_restore_start_sp_idle_leds();
                    rs_v3_post_start_all_commit();
                } else if ((now - s_blink_last_ms) >= RS_V3_BLINK_HALF_MS) {
                    s_blink_last_ms = now;
                    s_blink_on ^= 1u;
                    Led_Set(LED_BUT_START_SP, s_blink_on);
                    Led_Set(LED_STR_START_SP, s_blink_on);
                }
            }
        } else if (s_hold_start_sp != 0u) {
            const uint8_t was_commit = s_hold_start_sp_committed;
            s_hold_start_sp = 0u;
            s_hold_start_sp_ms = 0u;
            s_hold_start_sp_cd = 0u;
            s_hold_start_sp_cd_ms = 0u;
            s_hold_start_sp_committed = 0u;
            if (was_commit == 0u) {
                rs_v3_restore_beep_after_hold_cancel();
                rs_v3_restore_start_sp_idle_leds();
                rs_v3_post_start_sp();
            }
        }
    } else {
        /* Большая: короткий RELEASE → START_SP; hold ≥2с → без EVT. */
        if (fire_down != 0u) {
            if (s_btn_start_sp_down == 0u) {
                s_hold_start_sp_ms = 0u;
            } else if (s_hold_start_sp_ms < 0xFFF0u) {
                s_hold_start_sp_ms = (uint16_t)(s_hold_start_sp_ms + 10u);
            }
        } else if (s_btn_start_sp_down != 0u) {
            if (s_hold_start_sp_ms < RS_V3_SP_ARM_MS) {
                rs_v3_post_start_sp();
            }
            s_hold_start_sp_ms = 0u;
        }
    }
    /* Fire-ctrl: sticky same-seq в BuildRsp до ACK. Повтор post — только
     * s_*_evt_pending (слот был занят) / inflight watchdog. */
    s_btn_start_sp_down = fire_down;

    /* ——— ПУСК ОБЩИЙ hold 3с (большая; на малой FORCE может promote) ——— */
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
            rs_v3_start_hold_beep();
        } else if (s_hold_start_all_ms < RS_V3_HOLD_START_ALL_MS) {
            s_hold_start_all_ms = (uint16_t)(s_hold_start_all_ms + 10u);
        }
        if (s_hold_start_all_ms >= RS_V3_HOLD_START_ALL_MS &&
            s_hold_start_all_committed == 0u) {
            s_hold_start_all_committed = 1u;
            Beeper_StopPattern();
            rs_v3_restore_fault_beep_if_needed();
            rs_v3_restore_start_all_idle_leds();
            rs_v3_post_start_all_commit();
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
            rs_v3_restore_beep_after_hold_cancel();
        }
        s_hold_start_all_committed = 0u;
        rs_v3_restore_start_all_idle_leds();
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
            /* Цикл режима здесь (не в TouchGFX): иначе Drain до presenter шлёт старый mode. */
            uint8_t zi = MenuUi_GetBlockZoneSelected();
            uint8_t zone = (uint8_t)(zi + 1u);
            uint8_t mode;
            if (s_fire_active != 0u || zi >= 32u) {
                continue;
            }
            mode = PPKY_ZoneFireModeGet(zi);
            /* 0 → 2 → 3 → 0 (режим 4 «пожар выкл» в меню не листаем) */
            if (mode == 0u) {
                mode = 2u;
            } else if (mode == 2u) {
                mode = 3u;
            } else {
                mode = 0u;
            }
            PPKY_ZoneFireModeSet(zi, mode);
            PPKY_ZoneModeUiNotify(zi);
            SaveConfig();
            rs_v3_sync_led_stop_status();
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_ZONE_BLOCK_SET, zone, mode,
                                           0u, 0u);
        } else if (screen == RS_PANEL_SCREEN_MENU_DEVICES &&
                   ue->evt_type == (uint8_t)RS_PANEL_UI_EVT_CONFIRM) {
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_DEVICES_GET, 0u, 0u,
                                           (uint16_t)MenuUi_GetMcuDetailSlot(), 0u);
        }
    }

    /* ОСТАНОВ / ПУСК СП — только OnButtonSample (RELEASE / hold), не PRESS из Drain. */
    ctx->pending_ui_count = 0u;
    ctx->pending_btn_count = 0u;

    /* Prefetch журнала — только ordinary idle; critical может быть занят. */
    if (s_fire_active == 0u &&
        s_event.seq == 0u && s_evt_q_n == 0u) {
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
    /* LED_ERR/NORM: список, link lost, CLEAR_ALL→SET (resync), либо HAS_FAULTS
     * при уже показанном UI. Не гасить NORM на миг между CLEAR и SET. */
    uint8_t faults =
        (s_active_fault_count > 0u || link_lost != 0u ||
         s_fault_sound_resync != 0u ||
         (s_has_faults != 0u && s_fault_ui_pushed_count > 0u)) ? 1u : 0u;

    rs_v3_sync_power_led();

    if (fire != 0u || faults != 0u) {
        Led_Set(LED_NORM, 0u);
    } else if (s_sys_ready != 0u) {
        Led_Set(LED_NORM, 1u);
    } else {
        Led_Set(LED_NORM, 0u);
    }

    if (fire != 0u) {
        /* ПОЖАР1 — мигание 1,5 Гц. ПОЖАР2 и остальные пожарные статусы —
         * непрерывно. Пока ZONES нет — мигание ПОЖАР1, чтобы не показать
         * ложный непрерывный ПОЖАР2. Смесь зон: достаточно одной не-FIRE1. */
        uint8_t fire1;
        if (s_zones.count == 0u || s_zones_valid == 0u) {
            fire1 = 1u;
        } else {
            fire1 = rs_v3_zones_want_fire1();
        }
        Led_Set(LED_FIRE, (fire1 != 0u) ? LED_BLINK : 1u);
        Led_SetBrightness(LED_FIRE, LED_STATUS_MAX_BRIGHTNESS);
        Led_ForceStatusBright(LED_FIRE);
        /* LED_START — rs_v3_sync_fire_indication (тушение). */
    } else {
        Led_Set(LED_FIRE, 0u);
        if (s_fire_snd_phase == RS_V3_FIRE_SND_IDLE) {
            Led_Set(LED_START, 0u);
        }
    }
    rs_v3_sync_led_stop_status();

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

    RsPanelV3Slave_OnButtonSample();
    rs_v3_evt_retry_sticky();
    rs_v3_evt_flush_queue();
    rs_v3_evt_flush_crit();
    rs_v3_sync_status_leds();
    /* Запись каталога во Flash после quiet (не на каждый POLL). */
    if (s_catalog_flash_dirty != 0u &&
        (now - s_catalog_flash_dirty_ms) >= RS_V3_CATALOG_FLASH_QUIET_MS) {
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        rs_v3_catalog_flash_save_if_dirty();
    }
    /* Prefetch журнала — только ordinary idle; critical может быть занят. */
    if (s_fire_active == 0u &&
        s_event.seq == 0u && s_evt_q_n == 0u) {
        PanelJournalCache_PrefetchProcess();
    }

    if (s_fault_sound_phase == RS_V3_FS_WAIT_DUTY &&
        (int32_t)(now - s_fault_sound_deadline_ms) >= 0) {
        rs_v3_on_fault_signal_done();
    }

    /* Пакет FAULT_SET — один push после паузы; только после SYS_READY.
     * Во время CLEAR_ALL→SET (resync) не пушить placeholder при count==0 —
     * иначе UI потом отвергает 4→3 как «glitch» / остаётся старый список. */
    if (s_sys_ready != 0u && s_fault_ui_dirty != 0u &&
        (now - s_fault_ui_dirty_ms) >= RS_V3_FAULT_UI_QUIET_MS) {
        /* Не пушить «НОРМА» (n=0), пока HAS_FAULTS или идёт CLEAR→SET —
         * иначе миг центрального НОРМА при уходе одной из нескольких. */
        if (s_active_fault_count > 0u ||
            (s_has_faults == 0u && s_fault_sound_resync == 0u)) {
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
            } else if (s_has_faults != 0u) {
                /* SET так и не пришёл, но хост ещё сообщает о неисправностях —
                 * не мигать «НОРМА»; ждём дальше (CRC догонит). */
                s_fault_ui_dirty_ms = now;
                s_fault_resync_wait_ms = now;
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

}
