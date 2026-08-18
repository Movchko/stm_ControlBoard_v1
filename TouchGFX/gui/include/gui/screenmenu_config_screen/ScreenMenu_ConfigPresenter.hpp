#ifndef SCREENMENU_CONFIGPRESENTER_HPP
#define SCREENMENU_CONFIGPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ScreenMenu_ConfigView;

class ScreenMenu_ConfigPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ScreenMenu_ConfigPresenter(ScreenMenu_ConfigView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~ScreenMenu_ConfigPresenter() {}
#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
    virtual void onAppTick() override;
#endif
private:
    ScreenMenu_ConfigPresenter();

    ScreenMenu_ConfigView& view;

#ifndef SIMULATOR
    uint8_t lastState;
    uint8_t lastPercent;
    void updateStatusFromApp();
#endif
};

#endif // SCREENMENU_CONFIGPRESENTER_HPP
