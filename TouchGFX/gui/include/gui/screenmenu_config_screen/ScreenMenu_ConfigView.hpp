#ifndef SCREENMENU_CONFIGVIEW_HPP
#define SCREENMENU_CONFIGVIEW_HPP

#include <gui_generated/screenmenu_config_screen/ScreenMenu_ConfigViewBase.hpp>
#include <gui/screenmenu_config_screen/ScreenMenu_ConfigPresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class ScreenMenu_ConfigView : public ScreenMenu_ConfigViewBase
{
public:
    ScreenMenu_ConfigView();
    virtual ~ScreenMenu_ConfigView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setStatusText(const char* text);
protected:
#ifndef SIMULATOR
    static const uint16_t STATUS_TEXT_SIZE = 64;
    touchgfx::Unicode::UnicodeChar statusTextBuffer[STATUS_TEXT_SIZE];
    touchgfx::TextAreaWithOneWildcard statusText;
    void initStatusText();
#endif
};

#endif // SCREENMENU_CONFIGVIEW_HPP
