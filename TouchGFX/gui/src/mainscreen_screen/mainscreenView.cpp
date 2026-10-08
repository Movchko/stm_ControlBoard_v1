#include <gui/mainscreen_screen/mainscreenView.hpp>
#include <cstdio>
#include <cstring>
#include <cstddef>

#ifndef SIMULATOR
#include "main.h"
#include "panel_host_cache.h"
#include "config_zone_block.h"
#include "gost_mode.h"
#include "button.h"
#include "fire.h"
#include "led.h"
#include "menu_ui.h"
#include "tick_time.h"
#include "esp_manager.h"
#include "rs_panel_v3_slave.h"
#include "panel_link_monitor.h"

namespace {

constexpr uint32_t FIRE_NAME_HOLD_MS = 3000u;
constexpr uint32_t NEW_EVENT_HOLD_MS = 5000u;
constexpr uint32_t MAIN_NAV_AUTO_RETURN_MS = (uint32_t)LED_BUT_IDLE_TIMEOUT_TICKS * 10u;
constexpr uint8_t UI_LIST_CAPACITY = 16u;

enum FireNamePhase : uint8_t {
	PH_IDLE = 0,
	PH_WAIT_LONG_SCROLL,
	PH_HOLD_3S
};

/* Значения также задают порядок приоритета. */
enum UiBannerMode : uint8_t {
	BANNER_NONE = 0,
	BANNER_FIRE,
	BANNER_ATTENTION,
	BANNER_FAULT,
	BANNER_MODE,
	BANNER_WARNING = BANNER_FAULT /* совместимость со старым именем */
};

mainscreenView* g_main_view = nullptr;

uint8_t s_fn_n = 0u;
char s_fn_names[UI_LIST_CAPACITY][ZONE_NAME_SIZE + 1];
uint8_t s_fn_modes[UI_LIST_CAPACITY];
uint8_t s_fn_remaining[UI_LIST_CAPACITY];

uint8_t s_an_n = 0u;
char s_an_titles[UI_LIST_CAPACITY][WARNING_TITLE_LEN];
char s_an_details[UI_LIST_CAPACITY][ZONE_NAME_SIZE + 1];

uint8_t s_wn_n = 0u;
char s_wn_titles[UI_LIST_CAPACITY][WARNING_TITLE_LEN];
char s_wn_details[UI_LIST_CAPACITY][ZONE_NAME_SIZE + 1];

uint8_t s_mn_n = 0u;
uint8_t s_mn_zones[UI_LIST_CAPACITY];
char s_mn_names[UI_LIST_CAPACITY][ZONE_NAME_SIZE + 1];
uint8_t s_mn_modes[UI_LIST_CAPACITY];

uint8_t s_cur[5] = {0u};
FireNamePhase s_phase[5] = {PH_IDLE};
uint32_t s_hold_from[5] = {0u};
UiBannerMode s_banner_mode = BANNER_NONE;
UiBannerMode s_pending_banner = BANNER_NONE;
uint32_t s_pending_from = 0u;
char s_top_header_text[24] = {0};
char s_config_overlay_text[32] = {0};
uint8_t s_manual_browse = 0u;
uint32_t s_nav_last_press_ms = 0u;
/* Idle истёк, ждём полный проход бегущей (как автосмена в ППКУ1), затем возврат. */
uint8_t s_nav_return_after_marquee = 0u;
uint8_t s_fire_mode = 0u;
uint8_t s_fire_remaining = 0u;
uint8_t s_fire_active = 0u;
char s_fire_center_text[32] = {0};
uint8_t s_start_all_hold_shown = 0u;
#if GOST_MODE
uint8_t s_gost_force_fire_redraw = 0u;
#endif
/* Кэш текста бегущей неисправностей — не вызывать setText() на каждый Model::tick. */
UiBannerMode s_warn_marquee_banner = BANNER_NONE;
uint8_t s_warn_marquee_idx = 0xFFu;
char s_warn_marquee_text[ZONE_NAME_SIZE + 1] = {};
/* Кэш бегущей пожара: иначе fireShowCurrentZone() каждый force сбрасывает прокрутку. */
uint8_t s_fire_marquee_idx = 0xFFu;
char s_fire_marquee_text[ZONE_NAME_SIZE + 1] = {};

static void ui_invalidate_warn_marquee_cache(void)
{
	s_warn_marquee_banner = BANNER_NONE;
	s_warn_marquee_idx = 0xFFu;
	s_warn_marquee_text[0] = '\0';
}

static void ui_invalidate_fire_marquee_cache(void)
{
	s_fire_marquee_idx = 0xFFu;
	s_fire_marquee_text[0] = '\0';
}

static void ui_reset_banner_state(void)
{
	s_fn_n = 0u;
	memset(s_fn_modes, 0, sizeof(s_fn_modes));
	memset(s_fn_remaining, 0, sizeof(s_fn_remaining));
	s_an_n = 0u;
	s_wn_n = 0u;
	s_mn_n = 0u;
	memset(s_cur, 0, sizeof(s_cur));
	memset(s_phase, 0, sizeof(s_phase));
	memset(s_hold_from, 0, sizeof(s_hold_from));
	s_banner_mode = BANNER_NONE;
	s_pending_banner = BANNER_NONE;
	s_pending_from = 0u;
	s_top_header_text[0] = '\0';
	s_config_overlay_text[0] = '\0';
	s_manual_browse = 0u;
	s_nav_last_press_ms = 0u;
	s_nav_return_after_marquee = 0u;
	s_fire_mode = 0u;
	s_fire_remaining = 0u;
	s_fire_active = 0u;
	s_fire_center_text[0] = '\0';
	s_start_all_hold_shown = 0u;
#if GOST_MODE
	s_gost_force_fire_redraw = 0u;
#endif
	ui_invalidate_warn_marquee_cache();
	ui_invalidate_fire_marquee_cache();
}

static uint8_t fire_zone_mode_at(uint8_t idx)
{
	if (idx < s_fn_n) {
		return s_fn_modes[idx];
	}
	return s_fire_mode;
}

static uint8_t fire_zone_remaining_at(uint8_t idx)
{
	if (idx < s_fn_n) {
		return s_fn_remaining[idx];
	}
	return 0u;
}

static void fire_fill_center_text(uint8_t mode, uint8_t remaining_s, char *buf, size_t buf_sz)
{
	if (buf == nullptr || buf_sz == 0u) {
		return;
	}
	if (mode == 1u || mode == 5u) {
		(void)snprintf(buf, buf_sz, "%uСЕК.", (unsigned)remaining_s);
	} else if (mode == 2u) {
		(void)snprintf(buf, buf_sz, "ТУШЕНИЕ");
	} else if (mode == 3u) {
		(void)snprintf(buf, buf_sz, "ТУШ.ВЫП.");
	} else if (mode == 4u) {
		(void)snprintf(buf, buf_sz, "ПОЖАР/ОСТ.");
	} else if (mode == 6u) {
		(void)snprintf(buf, buf_sz, "ПОЖАР1");
	} else if (mode == 7u) {
		(void)snprintf(buf, buf_sz, "ТУШ.ОШ.");
	} else if (mode == 8u) {
		(void)snprintf(buf, buf_sz, "ПУСК ЗАБЛ.");
	} else if (mode == 9u) {
		(void)snprintf(buf, buf_sz, "ТУШ.ОСТ.");
	} else if (remaining_s > 0u) {
		(void)snprintf(buf, buf_sz, "%uСЕК.", (unsigned)remaining_s);
	} else {
		buf[0] = '\0';
	}
}

static bool ui_fire_blocks_events(void)
{
	if (s_fire_active == 0u) {
		return false;
	}
	for (uint8_t i = 0u; i < s_fn_n; ++i) {
		if (s_fn_modes[i] == 1u || s_fn_modes[i] == 2u) {
			return true;
		}
	}
	return (s_fn_n == 0u && (s_fire_mode == 1u || s_fire_mode == 2u));
}

/* Удержание ПУСК ОБЩИЙ: по факту кнопки/FSM, а не по кэшу UI (иначе после отпускания
 * таймер «залипает», пока не придёт другой апдейт). */
static bool ui_is_start_all_hold(void)
{
	return (Fire_IsStartAllHoldActive() != 0u);
}

static uint8_t ui_count(UiBannerMode banner)
{
	switch (banner) {
	case BANNER_FIRE: return s_fn_n;
	case BANNER_ATTENTION: return s_an_n;
	case BANNER_FAULT: return s_wn_n;
	case BANNER_MODE: return s_mn_n;
	default: return 0u;
	}
}

static UiBannerMode ui_highest_nonempty(void)
{
	for (uint8_t b = BANNER_FIRE; b <= BANNER_MODE; ++b) {
		if (ui_count((UiBannerMode)b) != 0u) {
			return (UiBannerMode)b;
		}
	}
	/* SYS_HAS_FAULTS без списка FAULT_EVT — всё равно не НОРМА. */
	if (RsPanelV3Slave_HasFaults() != 0u) {
		return BANNER_FAULT;
	}
	return BANNER_NONE;
}

static void ui_clear_phase(UiBannerMode banner)
{
	s_phase[banner] = PH_IDLE;
	s_hold_from[banner] = 0u;
}

static void ui_request_new_event(UiBannerMode banner, uint8_t item)
{
	if (ui_count(banner) == 0u) {
		return;
	}
	s_cur[banner] = (item < ui_count(banner)) ? item : 0u;
	ui_clear_phase(banner);
	s_pending_banner = banner;
	s_pending_from = HAL_GetTick();
}

static void trim_zone_name(char* dst, size_t dst_size, const int8_t* src)
{
	if (dst == nullptr || dst_size == 0u || src == nullptr) {
		return;
	}
	size_t n = 0u;
	while (n + 1u < dst_size && src[n] != 0) {
		dst[n] = (char)src[n];
		n++;
	}
	dst[n] = '\0';
	while (n > 0u && dst[n - 1u] == ' ') {
		dst[--n] = '\0';
	}
}

static void ui_refresh_mode_list(void)
{
	s_mn_n = 0u;
	for (uint8_t zi = 0u; zi < ZONE_NUMBER && s_mn_n < UI_LIST_CAPACITY; ++zi) {
		const uint8_t mode = PPKY_ZoneFireModeGet(zi);
		if (mode != 2u && mode != 3u) {
			continue;
		}
		const uint8_t pos = s_mn_n++;
		s_mn_zones[pos] = zi;
		s_mn_modes[pos] = mode;
		trim_zone_name(s_mn_names[pos], sizeof(s_mn_names[pos]),
			       PanelHostCache_GetConst()->zone_name[zi]);
	}
	if (s_mn_n == 0u) {
		s_cur[BANNER_MODE] = 0u;
	} else if (s_cur[BANNER_MODE] >= s_mn_n) {
		s_cur[BANNER_MODE] = 0u;
	}
	/* Новое событие списка РЕЖИМ — только через PPKY_ZoneModeUiNotify (меню). */
}

static UiBannerMode ui_highest_nonempty_with_sys(void)
{
	const UiBannerMode hi = ui_highest_nonempty();
#ifndef SIMULATOR
	/* HAS_FAULTS уже есть, а список во view ещё пуст (logo→main / до push) —
	 * всё равно FAULT, иначе ui_show_desired рисует НОРМА/залипает ПРОВЕРКА. */
	if (hi == BANNER_NONE &&
	    RsPanelV3Slave_IsSysReady() != 0u &&
	    RsPanelV3Slave_HasFaults() != 0u) {
		return BANNER_FAULT;
	}
#endif
	return hi;
}

static UiBannerMode ui_desired_banner(void)
{
	if (s_manual_browse && ui_count(s_banner_mode) > 0u) {
		return s_banner_mode;
	}
	if (s_pending_banner != BANNER_NONE) {
		if (TickAgeExpiredMs(HAL_GetTick(), s_pending_from, NEW_EVENT_HOLD_MS) != 0u) {
#if GOST_MODE
			/* После 5 с вспышки нового пожара — снова первый пришедший. */
			if (s_pending_banner == BANNER_FIRE && s_fn_n > 0u && !s_manual_browse) {
				s_cur[BANNER_FIRE] = 0u;
				s_gost_force_fire_redraw = 1u;
			}
#endif
			s_pending_banner = BANNER_NONE;
		} else if (!ui_fire_blocks_events() && ui_count(s_pending_banner) > 0u) {
			return s_pending_banner;
		}
	}
	return ui_highest_nonempty_with_sys();
}

static void ui_set_warning_header_visible(mainscreenView* view, bool visible)
{
	if (view != nullptr) {
		view->uiSetWarningHeaderVisible(visible);
	}
}

static void fire_marquee_done_thunk(CustomContainerSollText*)
{
	if (g_main_view == nullptr) {
		return;
	}
	if (s_banner_mode == BANNER_FIRE) {
		g_main_view->fireOnMarqueeOnePassDone();
	} else if (s_banner_mode == BANNER_ATTENTION || s_banner_mode == BANNER_FAULT ||
		   s_banner_mode == BANNER_MODE) {
		g_main_view->warningOnMarqueeOnePassDone();
	}
}

static uint8_t s_showing_proverka = 0u;
static uint8_t s_last_sys_ready = 0xFFu;

static void ui_clear_proverka_flags(void)
{
	s_showing_proverka = 0u;
	s_last_sys_ready = 1u;
	s_config_overlay_text[0] = '\0';
}

static void ui_show_desired(mainscreenView* view, bool force = false)
{
	if (view == nullptr) {
		return;
	}
#ifndef SIMULATOR
	/* Пока нет SYS_READY от ППКУ — «ПРОВЕРКА», кроме локальной «НЕТ СВЯЗИ». */
	const uint8_t sys_ready = RsPanelV3Slave_IsSysReady();
	if (sys_ready == 0u && PanelLinkMonitor_IsLost() == 0u) {
		view->uiShowConfigOverlay("ПРОВЕРКА");
		s_showing_proverka = 1u;
		s_banner_mode = BANNER_NONE;
		s_last_sys_ready = 0u;
		return;
	}
	if (sys_ready == 0u && PanelLinkMonitor_IsLost() != 0u) {
		/* Связи нет — показать WARN «НЕТ СВЯЗИ», не залипать в «ПРОВЕРКА». */
		force = true;
	}
	/* Пока центр ещё «ПРОВЕРКА» — всегда force. Флаги снимаем только после
	 * реальной отрисовки НОРМА/аварии (иначе early-return + шапка АВАРИЯ). */
	if (s_last_sys_ready != 1u || s_showing_proverka != 0u ||
	    s_config_overlay_text[0] != '\0') {
		force = true;
	}
#endif
	/* Таймер ПУСК ОБЩИЙ важнее НОРМА / прочих баннеров. */
	if (ui_is_start_all_hold()) {
		const uint8_t rem = RsPanelV3Slave_GetHoldRemainingSec();
		(void)std::snprintf(s_fire_center_text, sizeof(s_fire_center_text),
				    "%uСЕК.", (unsigned)rem);
		s_start_all_hold_shown = 1u;
		view->uiShowStartAllHoldTimer(s_fire_center_text);
		s_banner_mode = BANNER_NONE;
		ui_clear_proverka_flags();
		return;
	}
	/* Отпустили до 3 с — вернуть НОРМА / неисправности / режимы. */
	if (s_start_all_hold_shown != 0u) {
		s_start_all_hold_shown = 0u;
		s_fire_center_text[0] = '\0';
		force = true;
	}
	const UiBannerMode desired = ui_desired_banner();
#if GOST_MODE
	if (s_gost_force_fire_redraw != 0u) {
		s_gost_force_fire_redraw = 0u;
		force = true;
	}
#endif
	if (!force && desired == s_banner_mode) {
#ifndef SIMULATOR
		if (s_showing_proverka != 0u || s_config_overlay_text[0] != '\0') {
			force = true;
		} else
#endif
		{
			if (desired == BANNER_NONE) {
				ui_set_warning_header_visible(view, false);
			}
			return;
		}
	}
	if (desired == BANNER_FIRE) {
		view->fireShowCurrentZone();
		ui_clear_proverka_flags();
	} else if (desired == BANNER_ATTENTION || desired == BANNER_FAULT) {
		if (desired == BANNER_FAULT && s_wn_n == 0u &&
		    RsPanelV3Slave_HasFaults() != 0u) {
			s_wn_n = 1u;
			std::strncpy(s_wn_titles[0], "НЕИСПР.", WARNING_TITLE_LEN - 1u);
			s_wn_titles[0][WARNING_TITLE_LEN - 1u] = '\0';
			s_wn_details[0][0] = '\0';
			s_cur[BANNER_FAULT] = 0u;
		}
		s_banner_mode = desired;
		if (ui_count(desired) > 0u) {
			view->warningShowCurrent();
			ui_clear_proverka_flags();
		}
	} else if (desired == BANNER_MODE) {
		s_banner_mode = desired;
		if (s_mn_n > 0u) {
			view->modeShowCurrent();
			ui_clear_proverka_flags();
		}
	} else {
		view->uiShowNormalStatus();
		ui_set_warning_header_visible(view, false);
	}
}

static void ui_return_to_priority(mainscreenView* view)
{
	s_manual_browse = 0u;
	s_nav_last_press_ms = 0u;
	s_nav_return_after_marquee = 0u;
	Fire_UiSetManualSelection(0u, 0u);
	s_banner_mode = BANNER_NONE;
	memset(s_cur, 0, sizeof(s_cur));
	ui_invalidate_warn_marquee_cache();
	ui_show_desired(view, true);
}


static bool ui_warning_list_equals(uint8_t n, char (*titles)[WARNING_TITLE_LEN],
					   char (*details)[ZONE_NAME_SIZE + 1])
{
	if (n != s_wn_n) {
		return false;
	}
	for (uint8_t i = 0u; i < n; ++i) {
		if (std::strncmp(s_wn_titles[i], titles[i], WARNING_TITLE_LEN) != 0 ||
		    std::strncmp(s_wn_details[i], details[i], ZONE_NAME_SIZE + 1) != 0) {
			return false;
		}
	}
	return true;
}

static bool ui_attention_list_equals(uint8_t n, char (*titles)[WARNING_TITLE_LEN],
					     char (*details)[ZONE_NAME_SIZE + 1])
{
	if (n != s_an_n) {
		return false;
	}
	for (uint8_t i = 0u; i < n; ++i) {
		if (std::strncmp(s_an_titles[i], titles[i], WARNING_TITLE_LEN) != 0 ||
		    std::strncmp(s_an_details[i], details[i], ZONE_NAME_SIZE + 1) != 0) {
			return false;
		}
	}
	return true;
}

} // namespace
#endif

mainscreenView::mainscreenView()
{
}

void mainscreenView::setupScreen()
{
	mainscreenViewBase::setupScreen();
	CustomContainerSrollText.setText("");
#ifndef SIMULATOR
	g_main_view = this;
	CustomContainerSrollText.setFinishedCallback(fire_marquee_done_thunk);
	ui_reset_banner_state();
	textAreatime_top_bar.cancelMoveAnimation();
	textAreatime_top_bar.setPosition(0, 0, 128, 15);
	Fire_UiSetManualSelection(0u, 0u);
	ui_refresh_mode_list();
	if (RsPanelV3Slave_IsSysReady() == 0u && PanelLinkMonitor_IsLost() == 0u) {
		uiShowConfigOverlay("ПРОВЕРКА");
		s_showing_proverka = 1u;
		s_last_sys_ready = 0u;
	} else {
		/* Не uiShowNormalStatus(): иначе сбрасываем WARN из Model до sync-тика.
		 * Флаги «ПРОВЕРКА» снимет ui_show_desired после отрисовки. */
		s_showing_proverka = 1u;
		s_last_sys_ready = 0u;
		ui_show_desired(this, true);
	}
#endif
}

void mainscreenView::applyMuteIcon(bool soundOn)
{
#ifndef SIMULATOR
	/* Пока в top bar заголовок ПОЖАР/АВАРИЯ — mute не показываем (иначе наложение). */
	if (textAreatime_top_bar.isVisible()) {
		customContainerTopBar1.setMuteVisible(false);
		return;
	}
	customContainerTopBar1.setMuteVisible(!soundOn);
#else
	(void)soundOn;
#endif
}

void mainscreenView::applyWifiIcon(bool active)
{
#ifndef SIMULATOR
	/* Мигающий wifi поверх «ПОЖАР n/m» / «АВАРИЯ n/m» — не показывать, пока заголовок. */
	if (textAreatime_top_bar.isVisible()) {
		customContainerTopBar1.setWifiVisible(false);
		return;
	}
	customContainerTopBar1.setWifiVisible(active);
#else
	(void)active;
#endif
}

void mainscreenView::tearDownScreen()
{
#ifndef SIMULATOR
	if (g_main_view == this) {
		g_main_view = nullptr;
	}
	CustomContainerSrollText.setFinishedCallback(nullptr);
#endif
	mainscreenViewBase::tearDownScreen();
}

void mainscreenView::setDateTime(uint8_t hour, uint8_t min, uint8_t sec, uint8_t day, uint8_t month, uint8_t year)
{
#ifndef SIMULATOR
	if (textAreatime_top_bar.isVisible()) {
		return;
	}
#endif
	customContainerScrollTime1.setTime(hour, min, sec, day, month, year);
}

#ifndef SIMULATOR
void mainscreenView::fireOnMarqueeOnePassDone()
{
	if (s_manual_browse != 0u && s_nav_return_after_marquee != 0u) {
		/* Полный проход после idle — ещё hold 3 с (как автосмена в ППКУ1). */
		s_nav_return_after_marquee = 0u;
		s_phase[BANNER_FIRE] = PH_HOLD_3S;
		s_hold_from[BANNER_FIRE] = HAL_GetTick();
		return;
	}
#if GOST_MODE
	/* ГОСТ: без автосмены элемента — повторяем ту же бегущую строку. */
	if (s_banner_mode == BANNER_FIRE) {
		CustomContainerSrollText.restart();
	}
#else
	if (s_phase[BANNER_FIRE] == PH_WAIT_LONG_SCROLL) {
		s_phase[BANNER_FIRE] = PH_HOLD_3S;
		s_hold_from[BANNER_FIRE] = HAL_GetTick();
	}
#endif
}

void mainscreenView::warningOnMarqueeOnePassDone()
{
	if (s_manual_browse != 0u && s_nav_return_after_marquee != 0u &&
	    s_banner_mode >= BANNER_ATTENTION && s_banner_mode <= BANNER_MODE) {
		s_nav_return_after_marquee = 0u;
		s_phase[s_banner_mode] = PH_HOLD_3S;
		s_hold_from[s_banner_mode] = HAL_GetTick();
		return;
	}
#if GOST_MODE
	if (s_banner_mode == BANNER_ATTENTION || s_banner_mode == BANNER_FAULT ||
	    s_banner_mode == BANNER_MODE) {
		CustomContainerSrollText.restart();
	}
#else
	if (s_banner_mode >= BANNER_ATTENTION && s_banner_mode <= BANNER_MODE &&
	    s_phase[s_banner_mode] == PH_WAIT_LONG_SCROLL) {
		s_phase[s_banner_mode] = PH_HOLD_3S;
		s_hold_from[s_banner_mode] = HAL_GetTick();
	}
#endif
}

void mainscreenView::fireShowCurrentZone()
{
	if (s_fn_n == 0u) {
		return;
	}
	s_banner_mode = BANNER_FIRE;
	if (s_cur[BANNER_FIRE] >= s_fn_n) {
		s_cur[BANNER_FIRE] = 0u;
	}
#if GOST_MODE
	/* ГОСТ: статус центра привязан к показываемой зоне (домашняя = первая пришедшая). */
	if (!s_manual_browse &&
	    !(s_pending_banner == BANNER_FIRE &&
	      !TickAgeExpiredMs(HAL_GetTick(), s_pending_from, NEW_EVENT_HOLD_MS))) {
		s_cur[BANNER_FIRE] = 0u;
	}
	Fire_UiSetManualSelection(1u, s_cur[BANNER_FIRE]);
	{
		const uint8_t hdr_mode = fire_zone_mode_at(s_cur[BANNER_FIRE]);
		fire_fill_center_text(hdr_mode, fire_zone_remaining_at(s_cur[BANNER_FIRE]),
				      s_fire_center_text, sizeof(s_fire_center_text));
		const bool multi = (s_fn_n > 1u);
		const bool show_hdr = (multi || hdr_mode == 1u || hdr_mode == 5u ||
				       hdr_mode == 6u || hdr_mode == 8u || s_manual_browse);
		ui_set_warning_header_visible(this, show_hdr);
		char hdr[24];
		const char *base = "";
		if (hdr_mode == 1u) {
			base = "ДО ПУСКА";
		} else if (hdr_mode == 5u) {
			base = "ПАУЗА";
		} else {
			base = "ПОЖАР";
		}
		if (multi || s_manual_browse) {
			snprintf(hdr, sizeof(hdr), "%s %u/%u", base,
				 (unsigned)(s_cur[BANNER_FIRE] + 1u), (unsigned)s_fn_n);
			uiSetTopHeaderText(hdr);
		} else if (hdr_mode == 1u || hdr_mode == 5u) {
			uiSetTopHeaderText(base);
		} else if (hdr_mode == 6u || hdr_mode == 8u) {
			uiSetTopHeaderText("");
		}
	}
#else
	{
		const uint8_t hdr_mode = fire_zone_mode_at(s_cur[BANNER_FIRE]);
		fire_fill_center_text(hdr_mode, fire_zone_remaining_at(s_cur[BANNER_FIRE]),
				      s_fire_center_text, sizeof(s_fire_center_text));
		const bool show_hdr = (hdr_mode == 1u || hdr_mode == 5u ||
				       hdr_mode == 6u || hdr_mode == 8u || s_manual_browse);
		ui_set_warning_header_visible(this, show_hdr);
		char hdr[24];
		const char *base = "";
		if (hdr_mode == 1u) {
			base = "ДО ПУСКА";
		} else if (hdr_mode == 5u) {
			base = "ПАУЗА";
		} else if (s_manual_browse) {
			base = "ПОЖАР";
		}
		if (s_manual_browse && base[0] != '\0') {
			snprintf(hdr, sizeof(hdr), "%s %u/%u", base,
				 (unsigned)(s_cur[BANNER_FIRE] + 1u), (unsigned)s_fn_n);
			uiSetTopHeaderText(hdr);
		} else if (base[0] != '\0') {
			uiSetTopHeaderText(base);
		} else if (hdr_mode == 6u || hdr_mode == 8u) {
			uiSetTopHeaderText("");
		}
	}
#endif
	{
		const uint8_t fi = s_cur[BANNER_FIRE];
		const bool same_fire_marquee =
			(s_fire_marquee_idx == fi &&
			 std::strncmp(s_fire_marquee_text, s_fn_names[fi], ZONE_NAME_SIZE + 1) == 0);
		if (!same_fire_marquee) {
			s_fire_marquee_idx = fi;
			std::strncpy(s_fire_marquee_text, s_fn_names[fi], ZONE_NAME_SIZE);
			s_fire_marquee_text[ZONE_NAME_SIZE] = '\0';
			CustomContainerSrollText.setText(s_fn_names[fi]);
		}
	}
	ui_invalidate_warn_marquee_cache();
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(s_fire_center_text), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
#if GOST_MODE
	/* ГОСТ: без автопролистывания списка — фаза только для длинной бегущей строки. */
	ui_clear_phase(BANNER_FIRE);
	if (!s_manual_browse && !CustomContainerSrollText.isMarqueeFitting()) {
		s_phase[BANNER_FIRE] = PH_WAIT_LONG_SCROLL;
	}
#else
	if (s_manual_browse) {
		ui_clear_phase(BANNER_FIRE);
	} else if (CustomContainerSrollText.isMarqueeFitting()) {
		s_phase[BANNER_FIRE] = PH_HOLD_3S;
		s_hold_from[BANNER_FIRE] = HAL_GetTick();
	} else {
		s_phase[BANNER_FIRE] = PH_WAIT_LONG_SCROLL;
	}
#endif
}

void mainscreenView::warningShowCurrent()
{
	char (*titles)[WARNING_TITLE_LEN] = (s_banner_mode == BANNER_ATTENTION) ? s_an_titles : s_wn_titles;
	char (*details)[ZONE_NAME_SIZE + 1] = (s_banner_mode == BANNER_ATTENTION) ? s_an_details : s_wn_details;
	const uint8_t n = ui_count(s_banner_mode);
	if (n == 0u) {
		return;
	}
	const uint8_t cur = s_cur[s_banner_mode];
	ui_set_warning_header_visible(this, true);
	if (s_banner_mode == BANNER_ATTENTION) {
		char hdr[24];
		snprintf(hdr, sizeof(hdr), "ВНИМАНИЕ %u/%u", (unsigned)(cur + 1u), (unsigned)n);
		uiSetTopHeaderText(hdr);
	} else {
		uiUpdateWarningHeader((uint8_t)(cur + 1u), n);
	}
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(titles[cur]), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
	/* Не перезапускать бегущую строку, если текст тот же (иначе Model::tick сбрасывает прокрутку). */
	const bool same_marquee =
		(s_warn_marquee_banner == s_banner_mode && s_warn_marquee_idx == cur &&
		 std::strncmp(s_warn_marquee_text, details[cur], ZONE_NAME_SIZE + 1) == 0);
	if (!same_marquee) {
		s_warn_marquee_banner = s_banner_mode;
		s_warn_marquee_idx = cur;
		std::strncpy(s_warn_marquee_text, details[cur], ZONE_NAME_SIZE);
		s_warn_marquee_text[ZONE_NAME_SIZE] = '\0';
		CustomContainerSrollText.setText(details[cur]);
	}
#if GOST_MODE
	ui_clear_phase(s_banner_mode);
	if (!s_manual_browse && !CustomContainerSrollText.isMarqueeFitting()) {
		s_phase[s_banner_mode] = PH_WAIT_LONG_SCROLL;
	}
#else
	if (s_manual_browse) {
		ui_clear_phase(s_banner_mode);
	} else if (CustomContainerSrollText.isMarqueeFitting()) {
		s_phase[s_banner_mode] = PH_HOLD_3S;
		s_hold_from[s_banner_mode] = HAL_GetTick();
	} else {
		s_phase[s_banner_mode] = PH_WAIT_LONG_SCROLL;
	}
#endif
}

void mainscreenView::modeShowCurrent()
{
	if (s_mn_n == 0u) {
		return;
	}
	const uint8_t cur = s_cur[BANNER_MODE];
	s_banner_mode = BANNER_MODE;
	ui_set_warning_header_visible(this, true);
	char hdr[24];
	snprintf(hdr, sizeof(hdr), "РЕЖИМ %u/%u", (unsigned)(cur + 1u), (unsigned)s_mn_n);
	uiSetTopHeaderText(hdr);
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	const char* center = (s_mn_modes[cur] == 2u) ? "РУЧНОЙ" : "ЗАБЛОК.";
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(center), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
	CustomContainerSrollText.setText(s_mn_names[cur]);
	ui_invalidate_warn_marquee_cache();
#if GOST_MODE
	ui_clear_phase(BANNER_MODE);
	if (!s_manual_browse && !CustomContainerSrollText.isMarqueeFitting()) {
		s_phase[BANNER_MODE] = PH_WAIT_LONG_SCROLL;
	}
#else
	if (s_manual_browse) {
		ui_clear_phase(BANNER_MODE);
	} else if (CustomContainerSrollText.isMarqueeFitting()) {
		s_phase[BANNER_MODE] = PH_HOLD_3S;
		s_hold_from[BANNER_MODE] = HAL_GetTick();
	} else {
		s_phase[BANNER_MODE] = PH_WAIT_LONG_SCROLL;
	}
#endif
}

void mainscreenView::SetTime(uint32_t time)
{
	(void)time;
}

void mainscreenView::handleTickEvent()
{
	mainscreenViewBase::handleTickEvent();
#ifndef SIMULATOR
	static uint8_t s_cfg_overlay_was = 0u;
	if (MenuUi_IsConfigOverlayActive()) {
		s_cfg_overlay_was = 1u;
		return;
	}
	if (s_cfg_overlay_was != 0u) {
		s_cfg_overlay_was = 0u;
		/* После УСПЕХ/оверлея force: иначе BANNER_NONE==BANNER_NONE и НОРМА не рисуется. */
		ui_show_desired(this, true);
		return;
	}
#endif
	const uint32_t now = HAL_GetTick();
	ui_refresh_mode_list();

	uint8_t changed_zone;
	if (PPKY_ZoneModeUiConsumeNew(&changed_zone) != 0u) {
		for (uint8_t i = 0u; i < s_mn_n; ++i) {
			if (s_mn_zones[i] == changed_zone) {
				ui_request_new_event(BANNER_MODE, i);
				break;
			}
		}
	}
	if (s_manual_browse != 0u && s_nav_last_press_ms != 0u &&
	    TickAgeExpiredMs(now, s_nav_last_press_ms, MAIN_NAV_AUTO_RETURN_MS) != 0u) {
		const bool marquee_busy =
			!CustomContainerSrollText.isMarqueeFitting() &&
			CustomContainerSrollText.isMarqueeRunning();
		const bool hold_after_marquee =
			(s_banner_mode != BANNER_NONE &&
			 s_phase[s_banner_mode] == PH_HOLD_3S &&
			 s_hold_from[s_banner_mode] != 0u);
		if (marquee_busy) {
			/* Как автосмена ППКУ1: дождаться полного прохода бегущей. */
			s_nav_return_after_marquee = 1u;
		} else if (hold_after_marquee) {
			/* Уже ждём hold 3 с после прохода — ниже. */
		} else {
			ui_return_to_priority(this);
			return;
		}
	}
	/* Ручной browse: полный проход + hold 3 с → назад к 1-й (приоритет). */
	if (s_manual_browse != 0u && s_banner_mode != BANNER_NONE &&
	    s_phase[s_banner_mode] == PH_HOLD_3S && s_hold_from[s_banner_mode] != 0u &&
	    (now - s_hold_from[s_banner_mode]) >= FIRE_NAME_HOLD_MS &&
	    s_nav_last_press_ms != 0u &&
	    TickAgeExpiredMs(now, s_nav_last_press_ms, MAIN_NAV_AUTO_RETURN_MS) != 0u) {
		ui_return_to_priority(this);
		return;
	}
#if !GOST_MODE
	const UiBannerMode desired = ui_desired_banner();
	if (!s_manual_browse && desired != BANNER_NONE &&
	    s_phase[desired] == PH_HOLD_3S && s_hold_from[desired] != 0u &&
	    (now - s_hold_from[desired]) >= FIRE_NAME_HOLD_MS) {
		s_cur[desired] = (uint8_t)((s_cur[desired] + 1u) % ui_count(desired));
		ui_clear_phase(desired);
		s_banner_mode = BANNER_NONE;
		ui_show_desired(this, true);
		return;
	}
#endif
	ui_show_desired(this);
}

void mainscreenView::uiSetWarningHeaderVisible(bool visible)
{
	if (visible) {
		textAreatime_top_bar.cancelMoveAnimation();
		textAreatime_top_bar.setPosition(0, 0, 128, 15);
	}
	if (textAreatime_top_bar.isVisible() == visible) {
		if (visible) {
			/* Заголовок уже есть — top bar с wifi/mute всё равно не должен перекрывать. */
			if (customContainerTopBar1.isVisible()) {
				customContainerTopBar1.setVisible(false);
				customContainerTopBar1.invalidate();
			}
			textAreatime_top_bar.invalidate();
		}
		return;
	}
	/* Заголовок на всю ширину: прячем top bar (wifi/mute + чёрный iconsBackground). */
	customContainerTopBar1.setVisible(!visible);
	customContainerTopBar1.invalidate();
	customContainerScrollTime1.setVisible(!visible);
	customContainerScrollTime1.invalidate();
	textAreatime_top_bar.setVisible(visible);
	textAreatime_top_bar.invalidate();
#ifndef SIMULATOR
	if (!visible) {
		/* Вернуть mute/wifi после ухода с заголовка. */
		applyMuteIcon(PanelHostCache_GetConst()->beep != 0u);
		applyWifiIcon(EspManager_IsWifiIconVisible(HAL_GetTick()) != 0u);
	} else {
		customContainerTopBar1.setWifiVisible(false);
		customContainerTopBar1.setMuteVisible(false);
	}
#endif
}

void mainscreenView::uiUpdateWarningHeader(uint8_t cur_idx, uint8_t total)
{
	char hdr[24];
	snprintf(hdr, sizeof(hdr), "АВАРИЯ %u/%u", (unsigned)cur_idx, (unsigned)total);
	uiSetTopHeaderText(hdr);
}

void mainscreenView::uiSetTopHeaderText(const char* text)
{
	if (text == nullptr || std::strncmp(s_top_header_text, text, sizeof(s_top_header_text)) == 0) {
		return;
	}
	std::strncpy(s_top_header_text, text, sizeof(s_top_header_text) - 1u);
	s_top_header_text[sizeof(s_top_header_text) - 1u] = '\0';
	memset(textAreatime_top_barBuffer, 0, sizeof(textAreatime_top_barBuffer));
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(s_top_header_text),
			  textAreatime_top_barBuffer, TEXTAREATIME_TOP_BAR_SIZE);
	textAreatime_top_barBuffer[TEXTAREATIME_TOP_BAR_SIZE - 1u] = 0;
	textAreatime_top_bar.setWildcard(textAreatime_top_barBuffer);
	textAreatime_top_bar.invalidate();
}

void mainscreenView::uiShowNormalStatus()
{
	ui_set_warning_header_visible(this, false);
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("НОРМА"), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
	CustomContainerSrollText.setText("");
	ui_invalidate_warn_marquee_cache();
	s_banner_mode = BANNER_NONE;
	ui_clear_proverka_flags();
}

void mainscreenView::uiOnSysReady(void)
{
#ifndef SIMULATOR
	if (RsPanelV3Slave_IsSysReady() == 0u) {
		return;
	}
	/* Уже сняли оверлей и не в «ПРОВЕРКА» — не force каждый AppTick (сброс бегущей). */
	if (s_showing_proverka == 0u && s_last_sys_ready == 1u &&
	    s_config_overlay_text[0] == '\0') {
		return;
	}
	/* Не сбрасывать флаги до отрисовки — иначе early-return оставит центр «ПРОВЕРКА». */
	ui_show_desired(this, true);
#endif
}

void mainscreenView::uiShowConfigOverlay(const char* center_text)
{
	const char* txt = (center_text != nullptr && center_text[0] != '\0') ? center_text : "...";
	const bool same =
		(std::strncmp(s_config_overlay_text, txt, sizeof(s_config_overlay_text)) == 0);
	if (!same) {
		std::strncpy(s_config_overlay_text, txt, sizeof(s_config_overlay_text) - 1u);
		s_config_overlay_text[sizeof(s_config_overlay_text) - 1u] = '\0';
		memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(txt), textArea1Buffer, TEXTAREA1_SIZE);
		textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
		textArea1.setWildcard(textArea1Buffer);
		textArea1.invalidate();
	}
	/* Даже при том же тексте: убрать шапку АВАРИЯ/бегущую — иначе «ПРОВЕРКА»+АВАРИЯ. */
	ui_set_warning_header_visible(this, false);
	CustomContainerSrollText.setText("");
	ui_invalidate_warn_marquee_cache();
	s_banner_mode = BANNER_NONE;
}

void mainscreenView::uiShowStartAllHoldTimer(const char* center_text)
{
	ui_set_warning_header_visible(this, true);
	uiSetTopHeaderText("ДО ПУСКА");
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	const char* txt = (center_text != nullptr && center_text[0] != '\0') ? center_text : "0СЕК.";
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(txt), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
	CustomContainerSrollText.setText("");
	ui_invalidate_warn_marquee_cache();
	s_banner_mode = BANNER_NONE;
}

void mainscreenView::updateFireStatus(bool active, uint8_t mode, uint8_t zone, uint8_t remaining_s,
				      uint8_t nZoneNames, char (*zoneNames)[ZONE_NAME_SIZE + 1],
				      const uint8_t *zoneModes, const uint8_t *zoneRemaining)
{
	(void)zone;
	/* Model::tick шлёт статус каждый кадр — force только при реальном уходе с пожара,
	 * иначе warningShowCurrent()/setText каждый tick сбрасывает бегущую неисправностей. */
	const bool prev_active = (s_fire_active != 0u);
	fireUiActive = active;
	s_fire_active = active ? 1u : 0u;
	s_fire_mode = mode;
	s_fire_remaining = remaining_s;
	if (nZoneNames > UI_LIST_CAPACITY) {
		nZoneNames = UI_LIST_CAPACITY;
	}
	const uint8_t old_n = s_fn_n;
	uint8_t old_cur_mode = 0xffu;
	{
		uint8_t old_idx = s_cur[BANNER_FIRE];
		if (old_idx < old_n) {
			old_cur_mode = s_fn_modes[old_idx];
		}
	}
	bool changed = nZoneNames != s_fn_n;
	for (uint8_t i = 0u; i < nZoneNames && !changed; ++i) {
		changed = std::strncmp(s_fn_names[i], zoneNames[i], ZONE_NAME_SIZE + 1) != 0;
	}
#if GOST_MODE
	/* ГОСТ: вспышка 5 с — новый элемент (обычно снизу) или смена статуса пожара. */
	uint8_t flash_idx = 0u;
	bool need_flash = false;
	if (changed && active && nZoneNames > 0u) {
		if (nZoneNames > old_n) {
			flash_idx = (uint8_t)(nZoneNames - 1u);
			need_flash = true;
		} else if (old_n == 0u) {
			flash_idx = 0u;
			need_flash = true;
		} else {
			/* Тот же размер, но состав/порядок изменился — вспышка снизу. */
			for (uint8_t i = nZoneNames; i > 0u; --i) {
				const uint8_t idx = (uint8_t)(i - 1u);
				if (idx >= old_n ||
				    std::strncmp(s_fn_names[idx], zoneNames[idx], ZONE_NAME_SIZE + 1) != 0) {
					flash_idx = idx;
					need_flash = true;
					break;
				}
			}
		}
	}
#else
	(void)old_n;
	const bool first_changed = changed && nZoneNames > 0u &&
		(s_fn_n == 0u || std::strncmp(s_fn_names[0], zoneNames[0], ZONE_NAME_SIZE + 1) != 0);
#endif
	s_fn_n = active ? nZoneNames : 0u;
	static uint8_t last_mode = 0xffu;
	static uint8_t last_remaining = 0xffu;
	if (active) {
		for (uint8_t i = 0u; i < s_fn_n; ++i) {
			if (changed) {
				std::strncpy(s_fn_names[i], zoneNames[i], ZONE_NAME_SIZE);
				s_fn_names[i][ZONE_NAME_SIZE] = '\0';
			}
			s_fn_modes[i] = (zoneModes != nullptr) ? zoneModes[i] : mode;
			s_fn_remaining[i] = (zoneRemaining != nullptr) ? zoneRemaining[i] : remaining_s;
		}
		if (s_cur[BANNER_FIRE] >= s_fn_n) {
			s_cur[BANNER_FIRE] = 0u;
		}
	} else {
		memset(s_fn_modes, 0, sizeof(s_fn_modes));
		memset(s_fn_remaining, 0, sizeof(s_fn_remaining));
	}
	if (changed && active) {
		ui_clear_phase(BANNER_FIRE);
		ui_invalidate_fire_marquee_cache();
#if GOST_MODE
		if (need_flash) {
			ui_request_new_event(BANNER_FIRE, flash_idx);
		}
#else
		if (first_changed) {
			ui_request_new_event(BANNER_FIRE, 0u);
		}
#endif
	}
	if (!active) {
		Fire_UiSetManualSelection(0u, 0u);
		const bool leaving_fire = prev_active || (s_banner_mode == BANNER_FIRE);
		if (s_banner_mode == BANNER_FIRE) {
			s_banner_mode = BANNER_NONE;
		}
		s_fire_center_text[0] = '\0';
		ui_invalidate_fire_marquee_cache();
		if (leaving_fire) {
			last_mode = 0xffu;
			last_remaining = 0xffu;
		}
		ui_show_desired(this, leaving_fire);
		return;
	}
#if GOST_MODE
	uint8_t zmode;
	uint8_t zrem;
	uint8_t idx = s_cur[BANNER_FIRE];
	if (idx >= s_fn_n) {
		idx = 0u;
	}
	if (s_fn_n == 0u || Fire_IsStartAllHoldActive() != 0u) {
		zmode = mode;
		zrem = remaining_s;
	} else {
		zmode = fire_zone_mode_at(idx);
		zrem = fire_zone_remaining_at(idx);
	}
	const bool zone_mode_changed = (zmode != old_cur_mode);
#else
	uint8_t idx = s_cur[BANNER_FIRE];
	if (idx >= s_fn_n) {
		idx = 0u;
	}
	uint8_t zmode;
	uint8_t zrem;
	if (s_fn_n == 0u || Fire_IsStartAllHoldActive() != 0u) {
		zmode = mode;
		zrem = remaining_s;
	} else {
		zmode = fire_zone_mode_at(idx);
		zrem = fire_zone_remaining_at(idx);
	}
	(void)old_cur_mode;
#endif
	if (zmode != last_mode || zrem != last_remaining || s_banner_mode == BANNER_FIRE) {
		last_mode = zmode;
		last_remaining = zrem;
		fire_fill_center_text(zmode, zrem, s_fire_center_text, sizeof(s_fire_center_text));
		if (s_banner_mode == BANNER_FIRE) {
			memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
			Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(s_fire_center_text), textArea1Buffer, TEXTAREA1_SIZE);
			textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
			textArea1.setWildcard(textArea1Buffer);
			textArea1.invalidate();
		}
	}
#if GOST_MODE
	/* Смена статуса текущей зоны (тушение/останов/…) — тоже новое сообщение на 5 с. */
	bool status_flash = false;
	if (zone_mode_changed && zmode != 0u && !need_flash) {
		ui_request_new_event(BANNER_FIRE, idx);
		status_flash = true;
	}
	ui_show_desired(this, need_flash || status_flash);
#else
	ui_show_desired(this);
#endif
}

void mainscreenView::updateWarningStatus(bool active, uint8_t nItems, char (*bigTitles)[WARNING_TITLE_LEN],
					 char (*details)[ZONE_NAME_SIZE + 1])
{
	char attention_titles[UI_LIST_CAPACITY][WARNING_TITLE_LEN] = {};
	char attention_details[UI_LIST_CAPACITY][ZONE_NAME_SIZE + 1] = {};
	char fault_titles[UI_LIST_CAPACITY][WARNING_TITLE_LEN] = {};
	char fault_details[UI_LIST_CAPACITY][ZONE_NAME_SIZE + 1] = {};
	uint8_t attention_n = 0u;
	uint8_t fault_n = 0u;
	if (active) {
		for (uint8_t i = 0u; i < nItems && (attention_n + fault_n) < UI_LIST_CAPACITY; ++i) {
			const bool attention = ((uint8_t)bigTitles[i][0] == 0x01u);
			const char* title = attention ? &bigTitles[i][1] : bigTitles[i];
			char (*titles)[WARNING_TITLE_LEN] = attention ? attention_titles : fault_titles;
			char (*list_details)[ZONE_NAME_SIZE + 1] = attention ? attention_details : fault_details;
			uint8_t& count = attention ? attention_n : fault_n;
			std::strncpy(titles[count], title, WARNING_TITLE_LEN - 1u);
			titles[count][WARNING_TITLE_LEN - 1u] = '\0';
			std::strncpy(list_details[count], details[i], ZONE_NAME_SIZE);
			list_details[count][ZONE_NAME_SIZE] = '\0';
			++count;
		}
	}
	const bool attention_changed = !ui_attention_list_equals(attention_n, attention_titles, attention_details);
	const bool fault_changed = !ui_warning_list_equals(fault_n, fault_titles, fault_details);
	/* Кратковременное уменьшение n при CLEAR_ALL resync — не дёргать UI. */
	static uint32_t s_fault_ui_ms = 0u;
	const uint32_t now_ms = HAL_GetTick();
	const bool fault_shrink_glitch =
		(fault_n < s_wn_n && s_wn_n > 0u &&
		 s_fault_ui_ms != 0u && (now_ms - s_fault_ui_ms) < 3000u);
	bool need_force = false;
	if (attention_changed) {
		const bool attention_new =
			attention_n > s_an_n ||
			(attention_n > 0u && s_an_n > 0u &&
			 std::strncmp(s_an_titles[0], attention_titles[0], WARNING_TITLE_LEN) != 0);
		char keep_title[WARNING_TITLE_LEN];
		const uint8_t had_cur = (s_cur[BANNER_ATTENTION] < s_an_n) ? 1u : 0u;
		if (had_cur != 0u) {
			std::strncpy(keep_title, s_an_titles[s_cur[BANNER_ATTENTION]], WARNING_TITLE_LEN - 1u);
			keep_title[WARNING_TITLE_LEN - 1u] = '\0';
		}
		s_an_n = attention_n;
		memcpy(s_an_titles, attention_titles, sizeof(s_an_titles));
		memcpy(s_an_details, attention_details, sizeof(s_an_details));
		if (s_manual_browse != 0u && had_cur != 0u && s_an_n > 0u) {
			uint8_t found = 0xFFu;
			for (uint8_t i = 0u; i < s_an_n; ++i) {
				if (std::strncmp(s_an_titles[i], keep_title, WARNING_TITLE_LEN) == 0) {
					found = i;
					break;
				}
			}
			s_cur[BANNER_ATTENTION] = (found != 0xFFu) ? found : 0u;
		} else if (s_cur[BANNER_ATTENTION] >= s_an_n) {
			s_cur[BANNER_ATTENTION] = 0u;
		}
		if (attention_new && s_manual_browse == 0u) {
			ui_request_new_event(BANNER_ATTENTION, 0u);
			need_force = true;
		}
	}
	if (fault_changed && !fault_shrink_glitch) {
		const bool fault_grew = (fault_n > s_wn_n);
		const uint8_t prev_cur = s_cur[BANNER_FAULT];
		char keep_title[WARNING_TITLE_LEN];
		const uint8_t had_cur = (s_cur[BANNER_FAULT] < s_wn_n) ? 1u : 0u;
		if (had_cur != 0u) {
			std::strncpy(keep_title, s_wn_titles[s_cur[BANNER_FAULT]], WARNING_TITLE_LEN - 1u);
			keep_title[WARNING_TITLE_LEN - 1u] = '\0';
		}
		s_wn_n = fault_n;
		memcpy(s_wn_titles, fault_titles, sizeof(s_wn_titles));
		memcpy(s_wn_details, fault_details, sizeof(s_wn_details));
		s_fault_ui_ms = now_ms;
		if ((s_manual_browse != 0u || s_banner_mode == BANNER_FAULT) &&
		    had_cur != 0u && s_wn_n > 0u) {
			uint8_t found = 0xFFu;
			for (uint8_t i = 0u; i < s_wn_n; ++i) {
				if (std::strncmp(s_wn_titles[i], keep_title, WARNING_TITLE_LEN) == 0) {
					found = i;
					break;
				}
			}
			s_cur[BANNER_FAULT] = (found != 0xFFu) ? found : 0u;
		} else if (s_cur[BANNER_FAULT] >= s_wn_n) {
			s_cur[BANNER_FAULT] = 0u;
		}
		/* Не прыгать на первую и не сбрасывать бегущую при пересборке — только рост. */
		if (fault_grew && s_manual_browse == 0u) {
			ui_request_new_event(BANNER_FAULT, 0u);
			need_force = true;
		} else if (s_cur[BANNER_FAULT] != prev_cur) {
			need_force = true;
		} else if ((RsPanelV3Slave_IsSysReady() != 0u ||
			            PanelLinkMonitor_IsLost() != 0u) &&
			   s_showing_proverka == 0u &&
			   s_config_overlay_text[0] == '\0' &&
			   s_banner_mode == BANNER_FAULT && s_wn_n > 0u) {
			/* Только счётчик/текст текущей — без ui_clear_phase / рестарта hold.
			 * Не трогать UI пока на экране «ПРОВЕРКА» — иначе шапка АВАРИЯ поверх. */
			const uint8_t cur = s_cur[BANNER_FAULT];
			uiUpdateWarningHeader((uint8_t)(cur + 1u), s_wn_n);
			memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
			Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(s_wn_titles[cur]),
					  textArea1Buffer, TEXTAREA1_SIZE);
			textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
			textArea1.setWildcard(textArea1Buffer);
			textArea1.invalidate();
			ui_clear_proverka_flags();
			if (s_warn_marquee_banner != BANNER_FAULT || s_warn_marquee_idx != cur ||
			    std::strncmp(s_warn_marquee_text, s_wn_details[cur], ZONE_NAME_SIZE + 1) != 0) {
				s_warn_marquee_banner = BANNER_FAULT;
				s_warn_marquee_idx = cur;
				std::strncpy(s_warn_marquee_text, s_wn_details[cur], ZONE_NAME_SIZE);
				s_warn_marquee_text[ZONE_NAME_SIZE] = '\0';
				CustomContainerSrollText.setText(s_wn_details[cur]);
			}
		} else {
			need_force = true;
		}
	}
	/* Списки обновлены даже при пожаре; выбор дисплея делает ui_desired_banner(). */
	ui_show_desired(this, need_force);
}

void mainscreenView::handleMainNavButton(uint8_t but)
{
	if (but == BUT_ESC) {
		ui_return_to_priority(this);
		return;
	}
	if (but != BUT_UP && but != BUT_DOWN || ui_highest_nonempty() == BANNER_NONE) {
		return;
	}
	if (!s_manual_browse) {
		s_manual_browse = 1u;
		s_banner_mode = ui_desired_banner();
		if (s_banner_mode == BANNER_NONE) s_banner_mode = ui_highest_nonempty();
	}
	const int8_t direction = (but == BUT_UP) ? 1 : -1;
	const uint8_t count = ui_count(s_banner_mode);
	const uint8_t cur = s_cur[s_banner_mode];
	if ((direction > 0 && cur + 1u < count) || (direction < 0 && cur > 0u)) {
		s_cur[s_banner_mode] = (uint8_t)(cur + direction);
	} else {
		UiBannerMode next = s_banner_mode;
		do {
			next = (direction > 0) ? (next == BANNER_MODE ? BANNER_FIRE : (UiBannerMode)(next + 1u))
					       : (next == BANNER_FIRE ? BANNER_MODE : (UiBannerMode)(next - 1u));
		} while (ui_count(next) == 0u && next != s_banner_mode);
		s_banner_mode = next;
		s_cur[next] = (direction > 0) ? 0u : (uint8_t)(ui_count(next) - 1u);
	}
	s_nav_last_press_ms = HAL_GetTick();
	s_nav_return_after_marquee = 0u;
	ui_clear_phase(s_banner_mode);
	if (s_banner_mode == BANNER_FIRE) {
		Fire_UiSetManualSelection(1u, s_cur[BANNER_FIRE]);
	} else {
		Fire_UiSetManualSelection(0u, 0u);
	}
	ui_show_desired(this, true);
}
#endif
