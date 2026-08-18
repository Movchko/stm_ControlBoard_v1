#ifndef SCREENBLOCKZONEPRESENTER_HPP
#define SCREENBLOCKZONEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ScreenBlockZoneView;

class ScreenBlockZonePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ScreenBlockZonePresenter(ScreenBlockZoneView& v);

    virtual void activate();
    virtual void deactivate();
#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
#endif

    virtual ~ScreenBlockZonePresenter() {}

private:
    ScreenBlockZonePresenter();

    ScreenBlockZoneView& view;
};

#endif // SCREENBLOCKZONEPRESENTER_HPP
