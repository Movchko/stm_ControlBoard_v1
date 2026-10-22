#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>
#include "menu_ui.h"

ScreenMenu_ConnectionView::ScreenMenu_ConnectionView()
{
}

int16_t ScreenMenu_ConnectionView::getSelectedIndex() const
{
    return (int16_t)scrollWheel1.getSelectedItem();
}

void ScreenMenu_ConnectionView::applyVisibleItemCount(uint8_t count)
{
#ifndef SIMULATOR
    if (count == 0u) {
        count = 1u;
    }
    if ((uint8_t)scrollWheel1.getNumberOfItems() != count) {
        scrollWheel1.setNumberOfItems((int16_t)count);
        for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
            scrollWheel1.itemChanged(i);
        }
    }
#else
    (void)count;
#endif
}

void ScreenMenu_ConnectionView::setSelectedIndex(int16_t index)
{
    const int16_t n = (int16_t)scrollWheel1.getNumberOfItems();
    if (n <= 0) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= n) {
        index = (int16_t)(n - 1);
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

void ScreenMenu_ConnectionView::updateStatusLine(int16_t selectedId, bool wifiBlocked, bool wifiOn, bool rs485On)
{
#ifndef SIMULATOR
    char line[24] = {0};
    if (selectedId == (int16_t)MENU_CONN_ID_WIFI) {
        if (wifiBlocked) {
            (void)std::snprintf(line, sizeof(line), "БЛОК.");
        } else {
            (void)std::snprintf(line, sizeof(line), "%s", wifiOn ? "Вкл" : "Выкл");
        }
    } else if (selectedId == (int16_t)MENU_CONN_ID_RS485) {
        (void)std::snprintf(line, sizeof(line), "%s", rs485On ? "Вкл" : "Выкл");
    } else {
        line[0] = '\0';
    }
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), statusLineBuffer, STATUS_LINE_SIZE);
    statusLineBuffer[STATUS_LINE_SIZE - 1] = 0;
    statusLineText.invalidate();
#else
    (void)selectedId;
    (void)wifiBlocked;
    (void)wifiOn;
    (void)rs485On;
#endif
}

void ScreenMenu_ConnectionView::setupScreen()
{
    ScreenMenu_ConnectionViewBase::setupScreen();
#ifndef SIMULATOR
    initStatusLineText();
    applyVisibleItemCount(MenuUi_ConnVisibleCount());
    for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
        scrollWheel1.itemChanged(i);
    }
#else
    (void)0;
#endif
}

void ScreenMenu_ConnectionView::tearDownScreen()
{
    ScreenMenu_ConnectionViewBase::tearDownScreen();
}

#ifndef SIMULATOR
void ScreenMenu_ConnectionView::scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex)
{
    /* itemIndex — видимый индекс; в текст — логический id (пропуск скрытых). */
    const uint8_t id = MenuUi_ConnVisibleToId((uint8_t)itemIndex);
    item.updateConnectionText((int16_t)id);
}
#endif
