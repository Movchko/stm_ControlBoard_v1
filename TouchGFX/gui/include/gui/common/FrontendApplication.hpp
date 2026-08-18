#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

#include <gui_generated/common/FrontendApplicationBase.hpp>

class FrontendHeap;

using namespace touchgfx;

class FrontendApplication : public FrontendApplicationBase
{
public:
    FrontendApplication(Model& m, FrontendHeap& heap);
    virtual ~FrontendApplication() { }

    virtual void handleTickEvent() override;

    void gotoScreenMenuScreenNoTransition();
    void gotoScreenDevicesScreenNoTransition();
    void gotoScreenMenuConnectionScreenNoTransition();
    void gotoScreenMenuConfigScreenNoTransition();
    void gotoScreenMenuJurnalScreenNoTransition();
    void gotoScreenMenuMcuDetailsScreenNoTransition();
    void gotoScreenBlockZoneScreenNoTransition();

    Model& getModel() { return model; }

private:
    void gotoScreenMenuScreenNoTransitionImpl();
    void gotoScreenDevicesScreenNoTransitionImpl();
    void gotoScreenMenuConnectionScreenNoTransitionImpl();
    void gotoScreenMenuConfigScreenNoTransitionImpl();
    void gotoScreenMenuJurnalScreenNoTransitionImpl();
    void gotoScreenMenuMcuDetailsScreenNoTransitionImpl();
    void gotoScreenBlockZoneScreenNoTransitionImpl();

    touchgfx::Callback<FrontendApplication> screenMenuTransitionCallback;
    touchgfx::Callback<FrontendApplication> screenDevicesTransitionCallback;
    touchgfx::Callback<FrontendApplication> screenMenuConnectionTransitionCallback;
    touchgfx::Callback<FrontendApplication> screenMenuConfigTransitionCallback;
    touchgfx::Callback<FrontendApplication> screenMenuJurnalTransitionCallback;
    touchgfx::Callback<FrontendApplication> screenMenuMcuDetailsTransitionCallback;
    touchgfx::Callback<FrontendApplication> screenBlockZoneTransitionCallback;

#ifndef SIMULATOR
    static const int NUM_BUTTONS = 7;
    uint8_t prevButtonStates[NUM_BUTTONS];
#endif
};

#endif // FRONTENDAPPLICATION_HPP
