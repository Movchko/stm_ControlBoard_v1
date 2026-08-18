#include <gui/screenmenu_screen/ScreenMenuView.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/TypedText.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstdio>

#ifndef SIMULATOR
#include "beeper.h"
#include "led.h"
#include "gost_mode.h"
#endif

ScreenMenuView::ScreenMenuView()
#ifndef SIMULATOR
    : test_phase_(TEST_PHASE_IDLE),
      test_progress_(0)
#endif
{
}

int16_t ScreenMenuView::getSelectedMenuIndex() const
{
#if !defined(SIMULATOR) && GOST_MODE
    const int16_t max_idx = 5;
#else
    const int16_t max_idx = 6;
#endif
    int16_t idx = (int16_t)scrollWheel1.getSelectedItem();
    if (idx < 0) {
        return 0;
    }
    if (idx > max_idx) {
        return max_idx;
    }
    return idx;
}

void ScreenMenuView::setMenuIndex(int16_t index)
{
#if !defined(SIMULATOR) && GOST_MODE
    const int16_t max_idx = 5;
#else
    const int16_t max_idx = 6;
#endif
    if (index < 0) {
        index = 0;
    }
    if (index > max_idx) {
        index = max_idx;
    }
    scrollWheel1.animateToItem(index, 10);
}

#ifndef SIMULATOR
void ScreenMenuView::initParamLineText()
{
    textAreatime_2.setVisible(false);
    paramLineText.setPosition(0, 49, 128, 15);
    paramLineText.setColor(textAreatime_2.getColor());
    paramLineText.setLinespacing(0);
    paramLineText.setTypedText(touchgfx::TypedText(T___SINGLEUSE_2J37));
    paramLineText.setWildcard(paramLineBuffer);
    paramLineBuffer[0] = 0;
    add(paramLineText);
}

void ScreenMenuView::stopIndicationTestUi()
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

void ScreenMenuView::applyWipeProgress()
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

void ScreenMenuView::advanceIndicationTest()
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

    /* Фаза завершена — следующая. */
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
#endif

void ScreenMenuView::updateParameterLine(int16_t selectedIndex, uint8_t fireMode, bool soundOn, bool beepBlocked)
{
#ifndef SIMULATOR
    char line[48] = {0};
    if (selectedIndex == 0) {
        const char* modeName = "Автоматический";
        if (fireMode == 1u) {
            modeName = "Автономный";
        } else if (fireMode == 2u) {
            modeName = "Ручной";
        }
        (void)std::snprintf(line, sizeof(line), "%s", modeName);
    } else if (selectedIndex == 1) {
        if (beepBlocked) {
            (void)std::snprintf(line, sizeof(line), "%s БЛОК.", soundOn ? "Вкл" : "Откл");
        } else {
            (void)std::snprintf(line, sizeof(line), "%s", soundOn ? "Вкл" : "Откл");
        }
    } else {
        line[0] = '\0';
    }
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(line), paramLineBuffer, PARAM_LINE_SIZE);
    paramLineBuffer[PARAM_LINE_SIZE - 1] = 0;
    paramLineText.invalidate();
#else
    (void)selectedIndex;
    (void)fireMode;
    (void)soundOn;
    (void)beepBlocked;
#endif
}

void ScreenMenuView::setupScreen()
{
    ScreenMenuViewBase::setupScreen();
#ifndef SIMULATOR
#if GOST_MODE
    scrollWheel1.setNumberOfItems(6);
#else
    scrollWheel1.setNumberOfItems(7);
#endif
    initParamLineText();
    /* Поверх paramLineText: сначала белый, сверху чёрный (для перекрытия). */
    remove(box1_white);
    remove(box1_black);
    add(box1_white);
    add(box1_black);
    for (int i = 0; i < scrollWheel1ListItems.getNumberOfDrawables(); i++) {
        scrollWheel1.itemChanged(i);
        scrollWheel1ListItems[i].updateText(
#if GOST_MODE
            /* Логический i → подпись без «РЕЖИМ» (сдвиг +1). */
            (int16_t)(i + 1)
#else
            (int16_t)i
#endif
        );
    }
#endif
}

void ScreenMenuView::tearDownScreen()
{
#ifndef SIMULATOR
    stopIndicationTestUi();
#endif
    ScreenMenuViewBase::tearDownScreen();
}

void ScreenMenuView::handleTickEvent()
{
    ScreenMenuViewBase::handleTickEvent();
#ifndef SIMULATOR
    advanceIndicationTest();
#endif
}

#ifndef SIMULATOR
void ScreenMenuView::SetupMenuChangePos(uint8_t val)
{
    (void)val;
}

void ScreenMenuView::scrollWheel1UpdateItem(mainmenu& item, int16_t itemIndex)
{
#if GOST_MODE
    /* Логические 0..5 → подписи 1..6 (без глобального «РЕЖИМ»). */
    if (itemIndex < 0) {
        itemIndex = 0;
    }
    if (itemIndex > 5) {
        itemIndex = 5;
    }
    item.updateText((int16_t)(itemIndex + 1));
#else
    if (itemIndex > 6) {
        itemIndex = 6;
    }
    item.updateText(itemIndex);
#endif
}

void ScreenMenuView::startIndicationTest()
{
    if (test_phase_ != TEST_PHASE_IDLE) {
        return;
    }

    test_phase_ = TEST_PHASE_WHITE_DOWN;
    test_progress_ = 0;
    applyWipeProgress();
}
#endif
