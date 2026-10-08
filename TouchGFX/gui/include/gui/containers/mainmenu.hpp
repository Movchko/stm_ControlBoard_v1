#ifndef MAINMENU_HPP
#define MAINMENU_HPP

#include <gui_generated/containers/mainmenuBase.hpp>

class mainmenu : public mainmenuBase
{
public:
    mainmenu();
    virtual ~mainmenu() {}
    void updateText(int16_t value);
    void updateConnectionText(int16_t value);
    void updateTestSelectText(int16_t value);
    void updateTestLampText(int16_t value);
    void updateTestSoundText(int16_t value);
    virtual void initialize();
protected:
};

#endif // MAINMENU_HPP
