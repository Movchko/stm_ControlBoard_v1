#ifndef TESTSELECTSCREENVIEW_HPP
#define TESTSELECTSCREENVIEW_HPP

#include <gui_generated/testselectscreen_screen/TestSelectScreenViewBase.hpp>
#include <gui/testselectscreen_screen/TestSelectScreenPresenter.hpp>

class TestSelectScreenView : public TestSelectScreenViewBase
{
public:
    TestSelectScreenView();
    virtual ~TestSelectScreenView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent() override;

    int16_t getSelectedIndex() const;
    void setSelectedIndex(int16_t index);
    void updateDescription(int16_t index);

#ifndef SIMULATOR
    virtual void scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex) override;
    void startIndicationTest();
#endif

protected:
#ifndef SIMULATOR
    static const int16_t TEST_SCREEN_W = 128;
    static const int16_t TEST_SCREEN_H = 64;
    static const int16_t TEST_WIPE_STEP = 2;
    static const int16_t TEST_COUNT = 3;

    enum TestPhase : uint8_t {
        TEST_PHASE_IDLE = 0,
        TEST_PHASE_WHITE_DOWN,
        TEST_PHASE_BLACK_DOWN,
        TEST_PHASE_WHITE_RIGHT,
        TEST_PHASE_BLACK_RIGHT
    };

    void stopIndicationTestUi();
    void applyWipeProgress();
    void advanceIndicationTest();

    uint8_t test_phase_;
    int16_t test_progress_;
#endif
};

#endif // TESTSELECTSCREENVIEW_HPP
