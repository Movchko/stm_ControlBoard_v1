#include <gui/screenmenu_config_screen/ScreenMenu_ConfigView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>

ScreenMenu_ConfigView::ScreenMenu_ConfigView()
{
}

#ifndef SIMULATOR
void ScreenMenu_ConfigView::initStatusText()
{
    textAreatime_2.setVisible(false);
    statusText.setPosition(textAreatime_2.getX(), textAreatime_2.getY(),
                           textAreatime_2.getWidth(), textAreatime_2.getHeight());
    statusText.setColor(textAreatime_2.getColor());
    statusText.setLinespacing(textAreatime_2.getLinespacing());
    statusText.setTypedText(touchgfx::TypedText(T___SINGLEUSE_2J37));
    statusText.setWildcard(statusTextBuffer);
    add(statusText);
}
#endif

void ScreenMenu_ConfigView::setStatusText(const char* text)
{
#ifndef SIMULATOR
    if (text == nullptr) {
        statusTextBuffer[0] = 0;
    } else {
        Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), statusTextBuffer, STATUS_TEXT_SIZE);
        statusTextBuffer[STATUS_TEXT_SIZE - 1] = 0;
    }
    statusText.invalidate();
#else
    (void)text;
#endif
}

void ScreenMenu_ConfigView::setupScreen()
{
    ScreenMenu_ConfigViewBase::setupScreen();
#ifndef SIMULATOR
    initStatusText();
    setStatusText("Ожидание...");
#endif
}

void ScreenMenu_ConfigView::tearDownScreen()
{
    ScreenMenu_ConfigViewBase::tearDownScreen();
}
