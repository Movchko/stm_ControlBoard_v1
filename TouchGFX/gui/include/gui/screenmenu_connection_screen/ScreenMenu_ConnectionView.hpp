#ifndef SCREENMENU_CONNECTIONVIEW_HPP
#define SCREENMENU_CONNECTIONVIEW_HPP

#include <gui_generated/screenmenu_connection_screen/ScreenMenu_ConnectionViewBase.hpp>
#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class ScreenMenu_ConnectionView : public ScreenMenu_ConnectionViewBase
{
public:
    ScreenMenu_ConnectionView();
    virtual ~ScreenMenu_ConnectionView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    int16_t getSelectedIndex() const;
    void setSelectedIndex(int16_t index);
    void updateStatusLine(int16_t selectedIndex, bool wifiBlocked);

#ifndef SIMULATOR
    virtual void scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex) override;
#endif
protected:
#ifndef SIMULATOR
    static const uint16_t STATUS_LINE_SIZE = 24;
    touchgfx::Unicode::UnicodeChar statusLineBuffer[STATUS_LINE_SIZE];
    touchgfx::TextAreaWithOneWildcard statusLineText;
    void initStatusLineText();
#endif
};

#endif // SCREENMENU_CONNECTIONVIEW_HPP
