#ifndef TESTSELECTSCREENPRESENTER_HPP
#define TESTSELECTSCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class TestSelectScreenView;

class TestSelectScreenPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TestSelectScreenPresenter(TestSelectScreenView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~TestSelectScreenPresenter() {}

#ifndef SIMULATOR
    virtual void handleButton(uint8_t but, uint8_t state) override;
    virtual void onAppTick() override;
#endif

private:
    TestSelectScreenPresenter();

    TestSelectScreenView& view;

#ifndef SIMULATOR
    static const int16_t TEST_COUNT = 3;
    int16_t currentIndex;
    void refreshDescription();
#endif
};

#endif // TESTSELECTSCREENPRESENTER_HPP
