#ifndef MAINSCREENVIEW_HPP
#define MAINSCREENVIEW_HPP

#include <gui_generated/mainscreen_screen/mainscreenViewBase.hpp>
#include <gui/mainscreen_screen/mainscreenPresenter.hpp>

#ifndef SIMULATOR
#include "device_config.h"
#endif

class mainscreenView : public mainscreenViewBase
{
public:
    mainscreenView();
    virtual ~mainscreenView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent() override;

    /**
     * Передать текущее время/дату в контейнер часов.
     */
    void setDateTime(uint8_t hour, uint8_t min, uint8_t sec, uint8_t day, uint8_t month, uint8_t year);

#ifndef SIMULATOR
    virtual void SetTime(uint32_t time);
#endif

#ifndef SIMULATOR
    /** Таймер + имена зон по очереди (одно имя, ротация 3 с после полного показа). */
    void updateFireStatus(bool active, uint8_t mode, uint8_t zone, uint8_t remaining_s, uint8_t nZoneNames,
			  char (*zoneNames)[ZONE_NAME_SIZE + 1]);
    void updateWarningStatus(bool active, uint8_t nItems, char (*bigTitles)[WARNING_TITLE_LEN],
			     char (*details)[ZONE_NAME_SIZE + 1]);

    /** Один полный проход бегущей строки (длинное имя) — пауза 3 с и смена зоны. */
    void fireOnMarqueeOnePassDone();

    /** Показать текущее имя зоны в бегущей строке (доступ к protected CustomContainerSrollText). */
    void fireShowCurrentZone();
    void warningOnMarqueeOnePassDone();
    void warningShowCurrent();
    /** Показать текущую зону списка ручного/заблокированного режима. */
    void modeShowCurrent();
    void handleMainNavButton(uint8_t but);
    void uiSetWarningHeaderVisible(bool visible);
    void uiUpdateWarningHeader(uint8_t cur_idx, uint8_t total);
    void uiSetTopHeaderText(const char* text);
    /** Дежурный режим: по центру большого поля — «НОРМА». */
    void uiShowNormalStatus();
    /** Удержание ПУСК ОБЩИЙ: таймер 3с по центру (важнее «НОРМА»). */
    void uiShowStartAllHoldTimer(const char* center_text);
    /** imageMute в top_bar: показать при выключенном звуке. */
    void applyMuteIcon(bool soundOn);
    /** imageWifi в top_bar: показать при активном TCP-подключении хоста. */
    void applyWifiIcon(bool active);
#endif

protected:
    bool fireUiActive = false;
};

#endif // MAINSCREENVIEW_HPP
