#ifndef TESTSCREENPRESENTER_HPP
#define TESTSCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class TestScreenView;

class TestScreenPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TestScreenPresenter(TestScreenView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~TestScreenPresenter() {}

#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
#endif

private:
    TestScreenPresenter();

    TestScreenView& view;

#ifndef SIMULATOR
    int16_t currentIndex;
    void refreshStatus();
#endif
};

#endif // TESTSCREENPRESENTER_HPP
