#ifndef TESTSCREENVIEW_HPP
#define TESTSCREENVIEW_HPP

#include <gui_generated/testscreen_screen/TestScreenViewBase.hpp>
#include <gui/testscreen_screen/TestScreenPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class TestScreenView : public TestScreenViewBase
{
public:
    TestScreenView();
    virtual ~TestScreenView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    int16_t getSelectedIndex() const;
    void setSelectedIndex(int16_t index);
    void updateStatusLine(int16_t index, uint8_t on);

#ifndef SIMULATOR
    virtual void scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex) override;
    static const int16_t LAMP_COUNT = 15;
#endif

protected:
#ifndef SIMULATOR
    static const uint16_t STATUS_LINE_SIZE = 16;
    touchgfx::Unicode::UnicodeChar statusLineBuffer[STATUS_LINE_SIZE];
    touchgfx::TextAreaWithOneWildcard statusLineText;
    void initStatusLineText();
#endif
};

#endif // TESTSCREENVIEW_HPP
