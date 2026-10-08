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
#include "fire.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define RS_V3_HOLD_START_ALL_MS  3000u
#define RS_V3_HOLD_FIRE_RESET_MS 5000u
#define RS_V3_BLINK_HALF_MS      800u

static uint8_t s_sys_ready;
static uint8_t s_fire_active;
static uint8_t s_config_active;
static uint8_t s_has_faults;
static uint8_t s_power_input_fault; /* SYS POWER_INPUT_FAULT → мигание LED_POWER */
static uint16_t s_led_power_toggle_cnt; /* тики 10 мс; период = LED_POWER_TOOGLE_PERIOD_MS */

static RsPanelV3Event s_event;
static uint8_t s_event_seq_ctr;

static char s_zone_name_by_id[ZONE_NUMBER + 1u][RS_PANEL_V3_ZONE_NAME_LEN];
static uint8_t s_zone_name_valid[ZONE_NUMBER + 1u];
static RsPanelV3DeviceItem s_device_catalog[RS_PANEL_V3_MAX_CATALOG_MCU];
static uint16_t s_device_catalog_count;
static RsPanelV3FaultEvtItem s_active_faults[RS_PANEL_V3_MAX_FAULTS];
static uint16_t s_active_fault_count;
static uint16_t s_devices_crc;
static uint16_t s_faults_crc;
static uint8_t s_catalog_ready;
static uint8_t s_rsp_flags;

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

static void rs_v3_restore_start_all_idle_leds(void);

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
    s_devices_crc = 0u;
    s_faults_crc = 0u;
    s_catalog_ready = 0u;
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

uint8_t RsPanelV3Slave_PostEvent(uint8_t type, uint8_t zone, uint8_t u8_a,
                                 uint16_t u16_a, uint16_t u16_b)
{
    if (s_event.seq != 0u) {
        return 0u;
    }
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
    return 1u;
}

uint8_t RsPanelV3Slave_IsEventPending(void)
{
    return (s_event.seq != 0u) ? 1u : 0u;
}

static void rs_v3_restore_start_all_idle_leds(void)
{
    Led_SetBrightness(LED_BUT_START_ALL, LED_BUT_DIM_BRIGHTNESS);
    Led_SetBrightness(LED_STR_START_ALL, LED_BUT_DIM_BRIGHTNESS);
    Led_Set(LED_BUT_START_ALL, 0u);
    Led_Set(LED_STR_START_ALL, 1u);
}

static void rs_v3_recalc_catalog_crc(void)
{
    s_devices_crc = RsPanelV3_DeviceCatalogCrc(s_device_catalog, s_device_catalog_count);
    s_faults_crc = RsPanelV3_FaultEvtCrc(s_active_faults, s_active_fault_count);
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
    if (zone > 0u && zone <= ZONE_NUMBER && s_zone_name_valid[zone] != 0u) {
        strncpy(out, s_zone_name_by_id[zone], out_sz - 1u);
        out[out_sz - 1u] = '\0';
        return;
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

static void rs_v3_format_mku_detail_key(const RsPanelV3McuKey *key, char *out, uint16_t out_sz)
{
    if (out == 0 || out_sz == 0u) {
        return;
    }
    if (key == 0) {
        (void)snprintf(out, out_sz, "МКУ ?");
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
        rs_v3_format_mku_detail_key(&fe->mcu, out, out_sz);
        return;
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
        (void)snprintf(detail, detail_sz, "МКУ %u",
                       (unsigned)((mcu != 0) ? mcu->h_adr : fe->mcu.h_adr));
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
    /* Не в UART/OnPoll: иначе TouchGFX early-return оставляет центр «ПРОВЕРКА». */
    RsPanelEndpoint_QueueWarningUi((n > 0u) ? 1u : 0u, n, titles, details);
}

static void rs_v3_mark_fault_ui_dirty(void)
{
    s_fault_ui_dirty = 1u;
    s_fault_ui_dirty_ms = HAL_GetTick();
}

/* SYS_HAS_FAULTS уже есть, а FAULT_EVT ещё не дошли — не показывать НОРМА. */
static void rs_v3_push_fault_placeholder_ui(void)
{
    char titles[16][24];
    char details[16][ZONE_NAME_SIZE + 1];
    if (PanelLinkMonitor_IsLost() != 0u) {
        return;
    }
    if (s_sys_ready == 0u) {
        s_fault_ui_dirty = 1u;
        s_fault_ui_dirty_ms = HAL_GetTick();
        return;
    }
    if (s_fault_placeholder_active != 0u || s_fault_ui_dirty != 0u) {
        return;
    }
    memset(titles, 0, sizeof(titles));
    memset(details, 0, sizeof(details));
    (void)snprintf(titles[0], sizeof(titles[0]), "НЕИСПРАВНОСТЬ");
    s_fault_placeholder_active = 1u;
    RsPanelEndpoint_QueueWarningUi(1u, 1u, titles, details);
    if (MenuUi_IsMainScreenActive() == 0u || MenuUi_GetMenuSessionScreen() != 0u) {
        RsPanelEndpoint_QueueGotoMain();
    }
}

static void rs_v3_apply_zone_names(const RsPanelV3ZoneNameItem *zn)
{
    if (zn == 0) {
        return;
    }
    if (zn->op == (uint8_t)RS_PANEL_V3_STREAM_OP_CLEAR) {
        memset(s_zone_name_by_id, 0, sizeof(s_zone_name_by_id));
        memset(s_zone_name_valid, 0, sizeof(s_zone_name_valid));
        s_catalog_ready = 0u;
        s_rsp_flags |= RS_PANEL_V3_RSP_FLAG_NEED_CATALOG;
        return;
    }
    if (zn->zone == 0u || zn->zone > ZONE_NUMBER) {
        return;
    }
    strncpy(s_zone_name_by_id[zn->zone], zn->name, RS_PANEL_V3_ZONE_NAME_LEN - 1u);
    s_zone_name_by_id[zn->zone][RS_PANEL_V3_ZONE_NAME_LEN - 1u] = '\0';
    s_zone_name_valid[zn->zone] = 1u;
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
        s_rsp_flags |= RS_PANEL_V3_RSP_FLAG_NEED_CATALOG;
        rs_v3_recalc_catalog_crc();
        return;
    }
    if (s_device_catalog_count < RS_PANEL_V3_MAX_CATALOG_MCU) {
        s_device_catalog[s_device_catalog_count] = *dev;
        s_device_catalog_count++;
        rs_v3_recalc_catalog_crc();
    }
}

static void rs_v3_apply_fault_evt(const RsPanelV3FaultEvtItem *fe)
{
    uint16_t i;
    if (fe == 0) {
        return;
    }
    if (fe->op == (uint8_t)RS_PANEL_V3_FAULT_EVT_CLEAR_ALL) {
        s_active_fault_count = 0u;
        memset(s_active_faults, 0, sizeof(s_active_faults));
        /* Конец фазы resync каталога (зоны→устройства→CLEAR_ALL), даже если МКУ=0. */
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        rs_v3_recalc_catalog_crc();
        /* Не пушить UI сразу: держим прежний экран до пакета SET (quiet-timer).
         * Иначе АВАРИЯ n/n скачет 0→placeholder→1→2… */
        if (s_has_faults == 0u) {
            rs_v3_push_faults_ui();
            Beeper_StopPattern();
        } else {
            rs_v3_mark_fault_ui_dirty();
        }
        return;
    }
    if (fe->op == (uint8_t)RS_PANEL_V3_FAULT_EVT_SET) {
        /* Каталог без CLEAR_ALL — READY по любому SET (в т.ч. дубликату). */
        s_catalog_ready = 1u;
        s_rsp_flags &= (uint8_t)(~RS_PANEL_V3_RSP_FLAG_NEED_CATALOG);
        for (i = 0u; i < s_active_fault_count; i++) {
            if (RsPanelV3_FaultEvtEqual(&s_active_faults[i], fe) != 0u) {
                rs_v3_recalc_catalog_crc();
                return;
            }
        }
        if (s_active_fault_count < RS_PANEL_V3_MAX_FAULTS) {
            s_active_faults[s_active_fault_count] = *fe;
            s_active_fault_count++;
            Beeper_StartPulseTrain(SOUND_FAULT_DUTY_ON_MS, SOUND_FAULT_DUTY_OFF_MS,
                                   SOUND_FAULT_DUTY_PULSES, SOUND_FAULT_DUTY_REPEAT_MS);
            Led_Set(LED_ERR, 1u);
            Led_ForceStatusBright(LED_ERR);
            /* Новая неисправность: с меню/теста на MAIN (deactivate → Led_ExitTestMode).
             * Не ставить MainScreenActive=1 до GotoScreen — иначе переход пропустится. */
            if (MenuUi_IsMainScreenActive() == 0u || MenuUi_GetMenuSessionScreen() != 0u) {
                RsPanelEndpoint_QueueGotoMain();
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
            strncpy(names[i], z->items[i].name, ZONE_NAME_SIZE);
            names[i][ZONE_NAME_SIZE] = '\0';
            modes[i] = z->items[i].status;
            rems[i] = s_local_timer_s[i];
            if (i == 0u) {
                mode = modes[i];
                rem = rems[i];
            }
        }
        PanelUiBridge_SetFireStatus(active, mode, rem, n, names, modes, rems);
    }
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
    }

    {
        const uint8_t prev_ready = s_sys_ready;
        const uint8_t prev_cfg = s_config_active;
        s_sys_ready = ((poll.sys.flags & RS_PANEL_V3_SYS_READY) != 0u) ? 1u : 0u;
        s_fire_active = ((poll.sys.flags & RS_PANEL_V3_SYS_FIRE_ACTIVE) != 0u) ? 1u : 0u;
        s_config_active = ((poll.sys.flags & RS_PANEL_V3_SYS_CONFIG_ACTIVE) != 0u) ? 1u : 0u;
        s_has_faults = ((poll.sys.flags & RS_PANEL_V3_SYS_HAS_FAULTS) != 0u) ? 1u : 0u;
        s_power_input_fault =
            ((poll.sys.flags & RS_PANEL_V3_SYS_POWER_INPUT_FAULT) != 0u) ? 1u : 0u;

        if (prev_ready == 0u && s_sys_ready != 0u) {
            /* В UART только очередь: WARN → SYS_READY в ProcessDeferredUi (TouchGFX).
             * Прямой Notify/SetWarning из OnPoll снимал флаги «ПРОВЕРКА», а центр
             * не перерисовывался до UP/DOWN или меню. */
            MenuUi_SetMainScreenActive(1u);
            s_fault_ui_dirty = 0u;
            if (s_active_fault_count > 0u) {
                rs_v3_push_faults_ui();
            } else if (s_has_faults != 0u) {
                rs_v3_push_fault_placeholder_ui();
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
        }
        PanelJournalCache_PrefetchProcess();
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
            rs_v3_push_fault_placeholder_ui();
        } else if (s_has_faults == 0u && s_active_fault_count == 0u &&
                   s_fault_placeholder_active != 0u) {
            s_fault_placeholder_active = 0u;
            rs_v3_push_faults_ui();
        }
    }
    return 1u;
}

uint16_t RsPanelV3Slave_BuildRsp(uint8_t *dst, uint16_t dst_size)
{
    RsPanelV3Rsp rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.event = s_event;
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
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_START_ALL_COMMIT, 0u, 0u, 0u, 0u);
            rs_v3_restore_start_all_idle_leds();
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
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_FIRE_RESET, 0u, 0u, 0u, 0u);
        }
    } else if (s_hold_reset != 0u) {
        s_hold_reset = 0u;
        s_hold_reset_ms = 0u;
        s_hold_reset_committed = 0u;
    }

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
                (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_WIFI_SET, 0u, on, 0u, 0u);
            } else {
                uint8_t on = (PanelHostCache_GetConst()->rs485_on != 0u) ? 0u : 1u;
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
            (void)RsPanelV3Slave_PostEvent(RS_PANEL_V3_EVT_STOP_LAUNCH, s_selected_zone, 0u, 0u, 0u);
        }
    }

    ctx->pending_ui_count = 0u;
    ctx->pending_btn_count = 0u;

    PanelJournalCache_PrefetchProcess();
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
    uint8_t faults = (s_active_fault_count > 0u || s_has_faults != 0u ||
                      link_lost != 0u) ? 1u : 0u;

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
    } else {
        Led_Set(LED_FIRE, 0u);
        Led_Set(LED_START, 0u);
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
        rs_v3_push_fault_placeholder_ui();
    }
}

void RsPanelV3Slave_Timer10ms(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t i;

    RsPanelV3Slave_OnButtonSample();
    rs_v3_sync_status_leds();
    PanelJournalCache_PrefetchProcess();

    /* Пакет FAULT_SET — один push после паузы; только после SYS_READY. */
    if (s_sys_ready != 0u && s_fault_ui_dirty != 0u &&
        (now - s_fault_ui_dirty_ms) >= RS_V3_FAULT_UI_QUIET_MS) {
        if (s_active_fault_count > 0u || s_has_faults == 0u) {
            rs_v3_push_faults_ui();
        } else {
            s_fault_ui_dirty = 0u;
            rs_v3_push_fault_placeholder_ui();
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
