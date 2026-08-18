#ifndef SCREENMENU_CONNECTIONPRESENTER_HPP
#define SCREENMENU_CONNECTIONPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ScreenMenu_ConnectionView;

class ScreenMenu_ConnectionPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ScreenMenu_ConnectionPresenter(ScreenMenu_ConnectionView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~ScreenMenu_ConnectionPresenter() {}
#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
#endif
private:
    ScreenMenu_ConnectionPresenter();

    ScreenMenu_ConnectionView& view;

#ifndef SIMULATOR
    int16_t currentIndex;
    void refreshLine();
    void enterConfigScreen();
#endif
};

#endif // SCREENMENU_CONNECTIONPRESENTER_HPP
