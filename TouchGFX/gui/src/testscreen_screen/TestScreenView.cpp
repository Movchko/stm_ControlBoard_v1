#include <gui/testscreen_screen/TestScreenView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>

TestScreenView::TestScreenView()
#ifndef SIMULATOR
    : soundMode(false)
#endif
{
}

#ifndef SIMULATOR
int16_t TestScreenView::itemCount() const
{
    return soundMode ? SOUND_COUNT : LAMP_COUNT;
}
#endif

int16_t TestScreenView::getSelectedIndex() const
{
    int16_t idx = (int16_t)scrollWheel1.getSelectedItem();
    if (idx < 0) {
        return 0;
    }
#ifndef SIMULATOR
    const int16_t max = (int16_t)(itemCount() - 1);
    if (idx > max) {
        return max;
    }
#else
    if (idx > 14) {
        return 14;
    }
#endif
    return idx;
}

void TestScreenView::setSelectedIndex(int16_t index)
{
    if (index < 0) {
        index = 0;
    }
#ifndef SIMULATOR
    const int16_t max = (int16_t)(itemCount() - 1);
    if (index > max) {
        index = max;
    }
#else
    if (index > 14) {
        index = 14;
    }
#endif
    scrollWheel1.animateToItem(index, 10);
}

void TestScreenView::setSoundMode(bool sound)
{
#ifndef SIMULATOR
    soundMode = sound;
    scrollWheel1.setNumberOfItems(itemCount());
    for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
        scrollWheel1.itemChanged(i);
    }
#else
    (void)sound;
#endif
}

#ifndef SIMULATOR
void TestScreenView::initStatusLineText()
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

void TestScreenView::updateStatusLine(int16_t index, uint8_t on)
{
#ifndef SIMULATOR
    (void)index;
    char line[16] = {0};
    if (soundMode) {
        (void)std::snprintf(line, sizeof(line), "%s", (on != 0u) ? "Играет" : "Стоп");
    } else {
        (void)std::snprintf(line, sizeof(line), "%s", (on != 0u) ? "Вкл" : "Выкл");
    }
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), statusLineBuffer, STATUS_LINE_SIZE);
    statusLineBuffer[STATUS_LINE_SIZE - 1] = 0;
    statusLineText.invalidate();
#else
    (void)index;
    (void)on;
#endif
}

void TestScreenView::setupScreen()
{
    TestScreenViewBase::setupScreen();
#ifndef SIMULATOR
    scrollWheel1.setNumberOfItems(LAMP_COUNT);
    initStatusLineText();
    for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
        scrollWheel1.itemChanged(i);
    }
#endif
}

void TestScreenView::tearDownScreen()
{
    TestScreenViewBase::tearDownScreen();
}

#ifndef SIMULATOR
void TestScreenView::scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex)
{
    if (itemIndex < 0) {
        itemIndex = 0;
    }
    const int16_t max = (int16_t)(itemCount() - 1);
    if (itemIndex > max) {
        itemIndex = max;
    }
    if (soundMode) {
        item.updateTestSoundText(itemIndex);
    } else {
        item.updateTestLampText(itemIndex);
    }
}
#endif
