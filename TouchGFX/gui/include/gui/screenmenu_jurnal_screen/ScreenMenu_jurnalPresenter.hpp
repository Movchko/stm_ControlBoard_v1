#ifndef SCREENMENU_JURNALPRESENTER_HPP
#define SCREENMENU_JURNALPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ScreenMenu_jurnalView;

class ScreenMenu_jurnalPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ScreenMenu_jurnalPresenter(ScreenMenu_jurnalView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~ScreenMenu_jurnalPresenter() {}
#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
#endif
private:
    ScreenMenu_jurnalPresenter();

    ScreenMenu_jurnalView& view;
};

#endif // SCREENMENU_JURNALPRESENTER_HPP
