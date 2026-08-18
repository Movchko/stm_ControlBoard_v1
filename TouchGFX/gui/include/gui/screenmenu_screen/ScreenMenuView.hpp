#ifndef SCREENMENUVIEW_HPP
#define SCREENMENUVIEW_HPP

#include <gui_generated/screenmenu_screen/ScreenMenuViewBase.hpp>
#include <gui/screenmenu_screen/ScreenMenuPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class ScreenMenuView : public ScreenMenuViewBase
{
public:
    ScreenMenuView();
    virtual ~ScreenMenuView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent() override;

    int16_t getSelectedMenuIndex() const;
    void setMenuIndex(int16_t index);
    void updateParameterLine(int16_t selectedIndex, uint8_t fireMode, bool soundOn, bool beepBlocked);

#ifndef SIMULATOR
    virtual void SetupMenuChangePos(uint8_t val);
    virtual void scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex);
    void startIndicationTest();
#endif
protected:
#ifndef SIMULATOR
    static const uint16_t PARAM_LINE_SIZE = 40;
    static const int16_t TEST_SCREEN_W = 128;
    static const int16_t TEST_SCREEN_H = 64;
    /* Шаг за тик TouchGFX (~10 мс): 2 px — заметно и уложится в несколько секунд. */
    static const int16_t TEST_WIPE_STEP = 2;

    enum TestPhase : uint8_t {
        TEST_PHASE_IDLE = 0,
        TEST_PHASE_WHITE_DOWN,
        TEST_PHASE_BLACK_DOWN,
        TEST_PHASE_WHITE_RIGHT,
        TEST_PHASE_BLACK_RIGHT
    };

    touchgfx::Unicode::UnicodeChar paramLineBuffer[PARAM_LINE_SIZE];
    touchgfx::TextAreaWithOneWildcard paramLineText;
    void initParamLineText();
    void stopIndicationTestUi();
    void applyWipeProgress();
    void advanceIndicationTest();

    uint8_t test_phase_;
    int16_t test_progress_;
#endif
};

#endif // SCREENMENUVIEW_HPP
