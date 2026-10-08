#include <gui/testselectscreen_screen/TestSelectScreenView.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>

#ifndef SIMULATOR
#include "beeper.h"
#include "led.h"
#endif

TestSelectScreenView::TestSelectScreenView()
#ifndef SIMULATOR
    : test_phase_(TEST_PHASE_IDLE),
      test_progress_(0)
#endif
{
}

int16_t TestSelectScreenView::getSelectedIndex() const
{
    int16_t idx = (int16_t)scrollWheel1.getSelectedItem();
    if (idx < 0) {
        return 0;
    }
#ifndef SIMULATOR
    if (idx >= TEST_COUNT) {
        return (int16_t)(TEST_COUNT - 1);
    }
#else
    if (idx > 2) {
        return 2;
    }
#endif
    return idx;
}

void TestSelectScreenView::setSelectedIndex(int16_t index)
{
    if (index < 0) {
        index = 0;
    }
#ifndef SIMULATOR
    if (index >= TEST_COUNT) {
        index = (int16_t)(TEST_COUNT - 1);
    }
#else
    if (index > 2) {
        index = 2;
    }
#endif
    scrollWheel1.animateToItem(index, 10);
}

void TestSelectScreenView::updateDescription(int16_t index)
{
    const char* desc = "";
    switch (index) {
    case 0:
        desc = "Экран+звук+LED";
        break;
    case 1:
        desc = "Перекл. LED";
        break;
    case 2:
        desc = "Профили звука";
        break;
    default:
        break;
    }
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(desc),
                      textAreatime_2Buffer, TEXTAREATIME_2_SIZE);
    textAreatime_2Buffer[TEXTAREATIME_2_SIZE - 1] = 0;
    textAreatime_2.invalidate();
}

#ifndef SIMULATOR
void TestSelectScreenView::stopIndicationTestUi()
{
    test_phase_ = TEST_PHASE_IDLE;
    test_progress_ = 0;
    box1_white.setVisible(false);
    box1_black.setVisible(false);
    box1_white.setPosition(0, 0, TEST_SCREEN_W, TEST_SCREEN_H);
    box1_black.setPosition(0, 0, TEST_SCREEN_W, TEST_SCREEN_H);
    box1_white.invalidate();
    box1_black.invalidate();
}

void TestSelectScreenView::applyWipeProgress()
{
    switch (test_phase_) {
    case TEST_PHASE_WHITE_DOWN:
        box1_black.setVisible(false);
        box1_white.setVisible(true);
        box1_white.setPosition(0, 0, TEST_SCREEN_W, test_progress_);
        box1_white.invalidate();
        break;
    case TEST_PHASE_BLACK_DOWN:
        box1_white.setVisible(true);
        box1_white.setPosition(0, 0, TEST_SCREEN_W, TEST_SCREEN_H);
        box1_black.setVisible(true);
        box1_black.setPosition(0, 0, TEST_SCREEN_W, test_progress_);
        box1_white.invalidate();
        box1_black.invalidate();
        break;
    case TEST_PHASE_WHITE_RIGHT:
        box1_black.setVisible(false);
        box1_white.setVisible(true);
        box1_white.setPosition(0, 0, test_progress_, TEST_SCREEN_H);
        box1_white.invalidate();
        box1_black.invalidate();
        break;
    case TEST_PHASE_BLACK_RIGHT:
        box1_white.setVisible(true);
        box1_white.setPosition(0, 0, TEST_SCREEN_W, TEST_SCREEN_H);
        box1_black.setVisible(true);
        box1_black.setPosition(0, 0, test_progress_, TEST_SCREEN_H);
        box1_white.invalidate();
        box1_black.invalidate();
        break;
    default:
        break;
    }
}

void TestSelectScreenView::advanceIndicationTest()
{
    if (test_phase_ == TEST_PHASE_IDLE) {
        return;
    }

    const int16_t limit = (test_phase_ == TEST_PHASE_WHITE_DOWN ||
                           test_phase_ == TEST_PHASE_BLACK_DOWN)
                              ? TEST_SCREEN_H
                              : TEST_SCREEN_W;

    if (test_progress_ < limit) {
        test_progress_ = (int16_t)(test_progress_ + TEST_WIPE_STEP);
        if (test_progress_ > limit) {
            test_progress_ = limit;
        }
        applyWipeProgress();
        return;
    }

    switch (test_phase_) {
    case TEST_PHASE_WHITE_DOWN:
        test_phase_ = TEST_PHASE_BLACK_DOWN;
        test_progress_ = 0;
        applyWipeProgress();
        break;
    case TEST_PHASE_BLACK_DOWN:
        test_phase_ = TEST_PHASE_WHITE_RIGHT;
        test_progress_ = 0;
        applyWipeProgress();
        break;
    case TEST_PHASE_WHITE_RIGHT:
        test_phase_ = TEST_PHASE_BLACK_RIGHT;
        test_progress_ = 0;
        applyWipeProgress();
        break;
    case TEST_PHASE_BLACK_RIGHT:
    default:
        stopIndicationTestUi();
        Beeper_PlayIndicationTest();
        Led_RunIndicationSnake();
        break;
    }
}

void TestSelectScreenView::startIndicationTest()
{
    if (test_phase_ != TEST_PHASE_IDLE) {
        return;
    }
    test_phase_ = TEST_PHASE_WHITE_DOWN;
    test_progress_ = 0;
    applyWipeProgress();
}

void TestSelectScreenView::scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex)
{
    if (itemIndex < 0) {
        itemIndex = 0;
    }
    if (itemIndex >= TEST_COUNT) {
        itemIndex = (int16_t)(TEST_COUNT - 1);
    }
    item.updateTestSelectText(itemIndex);
}
#endif

void TestSelectScreenView::setupScreen()
{
    TestSelectScreenViewBase::setupScreen();
#ifndef SIMULATOR
    scrollWheel1.setNumberOfItems(TEST_COUNT);
    remove(box1_white);
    remove(box1_black);
    add(box1_white);
    add(box1_black);
    for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
        scrollWheel1.itemChanged(i);
    }
#endif
}

void TestSelectScreenView::tearDownScreen()
{
#ifndef SIMULATOR
    stopIndicationTestUi();
#endif
    TestSelectScreenViewBase::tearDownScreen();
}

void TestSelectScreenView::handleTickEvent()
{
    TestSelectScreenViewBase::handleTickEvent();
#ifndef SIMULATOR
    advanceIndicationTest();
#endif
}
