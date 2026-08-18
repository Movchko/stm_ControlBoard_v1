#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>

ScreenMenu_ConnectionView::ScreenMenu_ConnectionView()
{
}

int16_t ScreenMenu_ConnectionView::getSelectedIndex() const
{
    return (int16_t)scrollWheel1.getSelectedItem();
}

void ScreenMenu_ConnectionView::setSelectedIndex(int16_t index)
{
    if (index < 0) {
        index = 0;
    }
    if (index > 1) {
        index = 1;
    }
    scrollWheel1.animateToItem(index, 10);
}

#ifndef SIMULATOR
void ScreenMenu_ConnectionView::initStatusLineText()
{
    textAreatime_2.setVisible(false);
    statusLineText.setPosition(textAreatime_2.getX(), textAreatime_2.getY(),
                               textAreatime_2.getWidth(), textAreatime_2.getHeight());
    statusLineText.setColor(textAreatime_2.getColor());
    statusLineText.setLinespacing(0);
    statusLineText.setTypedText(touchgfx::TypedText(T___SINGLEUSE_2J37));
    statusLineText.setWildcard(statusLineBuffer);
    statusLineBuffer[0] = 0;
    add(statusLineText);
}
#endif

void ScreenMenu_ConnectionView::updateStatusLine(int16_t selectedIndex, bool wifiBlocked)
{
#ifndef SIMULATOR
    char line[24] = {0};
    if (selectedIndex == 0 && wifiBlocked) {
        (void)std::snprintf(line, sizeof(line), "БЛОК.");
    }
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), statusLineBuffer, STATUS_LINE_SIZE);
    statusLineBuffer[STATUS_LINE_SIZE - 1] = 0;
    statusLineText.invalidate();
#else
    (void)selectedIndex;
    (void)wifiBlocked;
#endif
}

void ScreenMenu_ConnectionView::setupScreen()
{
    ScreenMenu_ConnectionViewBase::setupScreen();
#ifndef SIMULATOR
    initStatusLineText();
    for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
        scrollWheel1.itemChanged(i);
    }
#endif
}

void ScreenMenu_ConnectionView::tearDownScreen()
{
    ScreenMenu_ConnectionViewBase::tearDownScreen();
}

#ifndef SIMULATOR
void ScreenMenu_ConnectionView::scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex)
{
    item.updateConnectionText(itemIndex);
}
#endif
