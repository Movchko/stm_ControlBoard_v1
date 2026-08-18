#include <gui/mainscreen_screen/mainscreenView.hpp>
#include <cstdio>
#include <cstring>

#ifndef SIMULATOR
#include "main.h"
#include "device_config.h"
#include "config_zone_block.h"
#include "gost_mode.h"
#include "button.h"
#include "fire.h"
#include "led.h"
#include "tick_time.h"

extern PPKYCfg PPKYConfig;

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
uint8_t s_manual_browse = 0u;
uint32_t s_nav_last_press_ms = 0u;
uint8_t s_fire_mode = 0u;
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

static void ui_invalidate_warn_marquee_cache(void)
{
	s_warn_marquee_banner = BANNER_NONE;
	s_warn_marquee_idx = 0xFFu;
	s_warn_marquee_text[0] = '\0';
}

static bool ui_fire_blocks_events(void)
{
	return (s_fire_active != 0u && (s_fire_mode == 1u || s_fire_mode == 2u));
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
		trim_zone_name(s_mn_names[pos], sizeof(s_mn_names[pos]), PPKYConfig.zone_name[zi]);
	}
	if (s_mn_n == 0u) {
		s_cur[BANNER_MODE] = 0u;
	} else if (s_cur[BANNER_MODE] >= s_mn_n) {
		s_cur[BANNER_MODE] = 0u;
	}
	/* Новое событие списка РЕЖИМ — только через PPKY_ZoneModeUiNotify (меню). */
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
	return ui_highest_nonempty();
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

static void ui_show_desired(mainscreenView* view, bool force = false)
{
	if (view == nullptr) {
		return;
	}
	/* Таймер ПУСК ОБЩИЙ важнее НОРМА / прочих баннеров. */
	if (ui_is_start_all_hold()) {
		s_start_all_hold_shown = 1u;
		view->uiShowStartAllHoldTimer(s_fire_center_text);
		s_banner_mode = BANNER_NONE;
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
		return;
	}
	if (desired == BANNER_FIRE) {
		view->fireShowCurrentZone();
	} else if (desired == BANNER_ATTENTION || desired == BANNER_FAULT) {
		s_banner_mode = desired;
		view->warningShowCurrent();
	} else if (desired == BANNER_MODE) {
		s_banner_mode = desired;
		view->modeShowCurrent();
	} else {
		view->uiShowNormalStatus();
		ui_set_warning_header_visible(view, false);
	}
}

static void ui_return_to_priority(mainscreenView* view)
{
	s_manual_browse = 0u;
	s_nav_last_press_ms = 0u;
	Fire_UiSetManualSelection(0u, 0u);
	s_banner_mode = BANNER_NONE;
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
	s_banner_mode = BANNER_NONE;
	s_manual_browse = 0u;
	s_nav_last_press_ms = 0u;
	memset(s_phase, 0, sizeof(s_phase));
	memset(s_hold_from, 0, sizeof(s_hold_from));
	Fire_UiSetManualSelection(0u, 0u);
	ui_refresh_mode_list();
	uiShowNormalStatus();
#endif
}

void mainscreenView::applyMuteIcon(bool soundOn)
{
#ifndef SIMULATOR
	customContainerTopBar1.setMuteVisible(!soundOn);
#else
	(void)soundOn;
#endif
}

void mainscreenView::applyWifiIcon(bool active)
{
#ifndef SIMULATOR
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
#if GOST_MODE
	/* ГОСТ: статус центра привязан к показываемой зоне (домашняя = первая пришедшая). */
	if (!s_manual_browse &&
	    !(s_pending_banner == BANNER_FIRE &&
	      !TickAgeExpiredMs(HAL_GetTick(), s_pending_from, NEW_EVENT_HOLD_MS))) {
		s_cur[BANNER_FIRE] = 0u;
	}
	Fire_UiSetManualSelection(1u, s_cur[BANNER_FIRE]);
	const bool multi = (s_fn_n > 1u);
	const bool show_hdr = (multi || s_fire_mode == 1u || s_fire_mode == 5u ||
			       s_fire_mode == 6u || s_fire_mode == 8u || s_manual_browse);
	ui_set_warning_header_visible(this, show_hdr);
	char hdr[24];
	const char *base = "";
	if (s_fire_mode == 1u) {
		base = "ДО ПУСКА";
	} else if (s_fire_mode == 5u) {
		base = "ПАУЗА";
	} else {
		base = "ПОЖАР";
	}
	if (multi || s_manual_browse) {
		snprintf(hdr, sizeof(hdr), "%s %u/%u", base,
			 (unsigned)(s_cur[BANNER_FIRE] + 1u), (unsigned)s_fn_n);
		uiSetTopHeaderText(hdr);
	} else if (s_fire_mode == 1u || s_fire_mode == 5u) {
		uiSetTopHeaderText(base);
	} else if (s_fire_mode == 6u || s_fire_mode == 8u) {
		uiSetTopHeaderText("");
	}
#else
	const bool show_hdr = (s_fire_mode == 1u || s_fire_mode == 5u ||
			       s_fire_mode == 6u || s_fire_mode == 8u || s_manual_browse);
	ui_set_warning_header_visible(this, show_hdr);
	char hdr[24];
	const char *base = "";
	if (s_fire_mode == 1u) {
		base = "ДО ПУСКА";
	} else if (s_fire_mode == 5u) {
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
	} else if (s_fire_mode == 6u || s_fire_mode == 8u) {
		uiSetTopHeaderText("");
	}
#endif
	CustomContainerSrollText.setText(s_fn_names[s_cur[BANNER_FIRE]]);
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
	if (s_manual_browse && s_nav_last_press_ms != 0u &&
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
	if (textAreatime_top_bar.isVisible() == visible) {
		return;
	}
	customContainerTopBar1.setVisible(true);
	customContainerTopBar1.invalidate();
	customContainerScrollTime1.setVisible(!visible);
	customContainerScrollTime1.invalidate();
	textAreatime_top_bar.setVisible(visible);
	textAreatime_top_bar.invalidate();
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
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("НОРМА"), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
	CustomContainerSrollText.setText("");
	ui_invalidate_warn_marquee_cache();
	s_banner_mode = BANNER_NONE;
}

void mainscreenView::uiShowStartAllHoldTimer(const char* center_text)
{
	ui_set_warning_header_visible(this, false);
	memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
	const char* txt = (center_text != nullptr && center_text[0] != '\0') ? center_text : "3С";
	Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(txt), textArea1Buffer, TEXTAREA1_SIZE);
	textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
	textArea1.setWildcard(textArea1Buffer);
	textArea1.invalidate();
	CustomContainerSrollText.setText("");
	ui_invalidate_warn_marquee_cache();
	s_banner_mode = BANNER_NONE;
}

void mainscreenView::updateFireStatus(bool active, uint8_t mode, uint8_t zone, uint8_t remaining_s,
				      uint8_t nZoneNames, char (*zoneNames)[ZONE_NAME_SIZE + 1])
{
	(void)zone;
	/* Model::tick шлёт статус каждый кадр — force только при реальном уходе с пожара,
	 * иначе warningShowCurrent()/setText каждый tick сбрасывает бегущую неисправностей. */
	const bool prev_active = (s_fire_active != 0u);
	fireUiActive = active;
	s_fire_active = active ? 1u : 0u;
	s_fire_mode = mode;
	if (nZoneNames > UI_LIST_CAPACITY) {
		nZoneNames = UI_LIST_CAPACITY;
	}
	const uint8_t old_n = s_fn_n;
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
	if (changed && active) {
		for (uint8_t i = 0u; i < s_fn_n; ++i) {
			std::strncpy(s_fn_names[i], zoneNames[i], ZONE_NAME_SIZE);
			s_fn_names[i][ZONE_NAME_SIZE] = '\0';
		}
		if (s_cur[BANNER_FIRE] >= s_fn_n) {
			s_cur[BANNER_FIRE] = 0u;
		}
		ui_clear_phase(BANNER_FIRE);
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
		if (leaving_fire) {
			last_mode = 0xffu;
			last_remaining = 0xffu;
		}
		ui_show_desired(this, leaving_fire);
		return;
	}
#if GOST_MODE
	const bool mode_changed = (mode != last_mode);
#endif
	if (mode != last_mode || remaining_s != last_remaining || s_banner_mode == BANNER_FIRE) {
		last_mode = mode;
		last_remaining = remaining_s;
		char buf[32];
		if (mode == 1u || mode == 5u) snprintf(buf, sizeof(buf), "%uС", (unsigned)remaining_s);
		else if (mode == 2u) snprintf(buf, sizeof(buf), "ТУШЕНИЕ");
		else if (mode == 3u) snprintf(buf, sizeof(buf), "ТУШ.ВЫП.");
		else if (mode == 4u) snprintf(buf, sizeof(buf), "ПОЖАР/ОСТ.");
		else if (mode == 6u) snprintf(buf, sizeof(buf), "ПОЖАР1");
		else if (mode == 7u) snprintf(buf, sizeof(buf), "ТУШ.ОШ.");
		else if (mode == 8u) snprintf(buf, sizeof(buf), "ПУСК ЗАБЛ.");
		else if (mode == 9u) snprintf(buf, sizeof(buf), "ТУШ.ОСТ.");
		else if (remaining_s > 0u) snprintf(buf, sizeof(buf), "%u", (unsigned)remaining_s);
		else buf[0] = '\0';
		std::strncpy(s_fire_center_text, buf, sizeof(s_fire_center_text) - 1u);
		s_fire_center_text[sizeof(s_fire_center_text) - 1u] = '\0';
		if (s_banner_mode == BANNER_FIRE) {
			memset(textArea1Buffer, 0, sizeof(textArea1Buffer));
			Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(s_fire_center_text), textArea1Buffer, TEXTAREA1_SIZE);
			textArea1Buffer[TEXTAREA1_SIZE - 1u] = 0;
			textArea1.setWildcard(textArea1Buffer);
			textArea1.invalidate();
		}
	}
#if GOST_MODE
	/* Смена статуса (тушение/останов/…) — тоже новое сообщение на 5 с. */
	bool status_flash = false;
	if (mode_changed && mode != 0u && !need_flash) {
		uint8_t idx = s_cur[BANNER_FIRE];
		if (idx >= s_fn_n) {
			idx = 0u;
		}
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
	const bool attention_first_changed = attention_changed && attention_n > 0u &&
		(s_an_n == 0u || std::strncmp(s_an_titles[0], attention_titles[0], WARNING_TITLE_LEN) != 0);
	const bool fault_first_changed = fault_changed && fault_n > 0u &&
		(s_wn_n == 0u || std::strncmp(s_wn_titles[0], fault_titles[0], WARNING_TITLE_LEN) != 0);
	if (attention_changed) {
		s_an_n = attention_n;
		memcpy(s_an_titles, attention_titles, sizeof(s_an_titles));
		memcpy(s_an_details, attention_details, sizeof(s_an_details));
		if (s_cur[BANNER_ATTENTION] >= s_an_n) s_cur[BANNER_ATTENTION] = 0u;
		ui_clear_phase(BANNER_ATTENTION);
	}
	if (fault_changed) {
		s_wn_n = fault_n;
		memcpy(s_wn_titles, fault_titles, sizeof(s_wn_titles));
		memcpy(s_wn_details, fault_details, sizeof(s_wn_details));
		if (s_cur[BANNER_FAULT] >= s_wn_n) s_cur[BANNER_FAULT] = 0u;
		ui_clear_phase(BANNER_FAULT);
	}
	if (attention_first_changed) ui_request_new_event(BANNER_ATTENTION, 0u);
	if (fault_first_changed) ui_request_new_event(BANNER_FAULT, 0u);
	/* Списки обновлены даже при пожаре; выбор дисплея делает ui_desired_banner(). */
	ui_show_desired(this, attention_changed || fault_changed);
}

void mainscreenView::handleMainNavButton(uint8_t but)
{
	if (but == BUT_ESC) {
		if (s_manual_browse) {
			ui_return_to_priority(this);
		}
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
	if (s_banner_mode == BANNER_FIRE) {
		Fire_UiSetManualSelection(1u, s_cur[BANNER_FIRE]);
	} else {
		Fire_UiSetManualSelection(0u, 0u);
	}
	ui_show_desired(this, true);
}
#endif
