#ifndef SCREENMENU_MCU_DETAILSPRESENTER_HPP
#define SCREENMENU_MCU_DETAILSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ScreenMenu_MCU_DetailsView;

class ScreenMenu_MCU_DetailsPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ScreenMenu_MCU_DetailsPresenter(ScreenMenu_MCU_DetailsView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~ScreenMenu_MCU_DetailsPresenter() {}
#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
    virtual void onAppTick() override;
#endif
private:
    ScreenMenu_MCU_DetailsPresenter();

    ScreenMenu_MCU_DetailsView& view;
};

#endif // SCREENMENU_MCU_DETAILSPRESENTER_HPP
