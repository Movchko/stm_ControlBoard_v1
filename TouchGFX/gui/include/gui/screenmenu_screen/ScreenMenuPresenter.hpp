#ifndef SCREENMENUPRESENTER_HPP
#define SCREENMENUPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

#ifndef SIMULATOR
#include "gost_mode.h"
#endif

using namespace touchgfx;

class ScreenMenuView;

class ScreenMenuPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ScreenMenuPresenter(ScreenMenuView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~ScreenMenuPresenter() {}
#ifndef SIMULATOR
    virtual void SetupMenuChangePos(unsigned char val);
    virtual void handleButton(uint8_t but, uint8_t state) override;
    virtual void onAppTick() override;
    virtual void onSoundOnChanged(bool soundOn) override;
#endif
private:
    ScreenMenuPresenter();

    ScreenMenuView& view;

#ifndef SIMULATOR
#if GOST_MODE
    /* Без пункта «РЕЖИМ» (глобальный fire_mode) — только «РЕЖИМ ЗОН». */
    static const int MENU_ITEMS = 6;
#else
    static const int MENU_ITEMS = 7;
#endif
    bool soundOn;
    int16_t currentIndex;
    void refreshLine();
    /** Логический индекс меню → «старый» индекс действий/подписей (0=РЕЖИМ …). */
    static int menuActionIndex(int16_t logical_index);
#endif
};

#endif // SCREENMENUPRESENTER_HPP
