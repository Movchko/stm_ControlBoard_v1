#include <gui/common/FrontendApplication.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/mainscreen_screen/mainscreenView.hpp>
#include <gui/mainscreen_screen/mainscreenPresenter.hpp>
#include <gui/screenmenu_screen/ScreenMenuView.hpp>
#include <gui/screenmenu_screen/ScreenMenuPresenter.hpp>
#include <gui/screen_devices_screen/screen_devicesView.hpp>
#include <gui/screen_devices_screen/screen_devicesPresenter.hpp>
#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionView.hpp>
#include <gui/screenmenu_connection_screen/ScreenMenu_ConnectionPresenter.hpp>
#include <gui/screenmenu_config_screen/ScreenMenu_ConfigView.hpp>
#include <gui/screenmenu_config_screen/ScreenMenu_ConfigPresenter.hpp>
#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalView.hpp>
#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalPresenter.hpp>
#include <gui/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsView.hpp>
#include <gui/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsPresenter.hpp>
#include <gui/screenblockzone_screen/ScreenBlockZoneView.hpp>
#include <gui/screenblockzone_screen/ScreenBlockZonePresenter.hpp>
#include <touchgfx/transitions/NoTransition.hpp>

#ifndef SIMULATOR
#include "button.h"
#include "fire.h"
#include "menu_ui.h"
#endif

using namespace touchgfx;

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap)
{
#ifndef SIMULATOR
    for (int i = 0; i < NUM_BUTTONS; i++)
        prevButtonStates[i] = 0;
#endif
}

void FrontendApplication::handleTickEvent()
{
    model.tick();

#ifndef SIMULATOR
    /* Как при ПОЖАРЕ: удержание ПУСК ОБЩИЙ из меню/подменю — на главный экран
     * (там виден 3с счётчик общего пуска). */
    if ((Fire_IsActive() || Fire_IsStartAllHoldActive()) && !MenuUi_IsMainScreenActive()) {
        if (MenuUi_IsConfigSessionActive()) {
            Esp32_SetEnabled(0u);
            MenuUi_SetConfigSession(0u);
        }
        gotomainscreenScreenNoTransition();
        FrontendApplicationBase::handleTickEvent();
        return;
    }

    ModelListener* listener = model.getModelListener();
    if (listener)
    {
        for (int but = 0; but < NUM_BUTTONS; but++)
        {
            uint8_t st = (uint8_t)Button_GetState((uint8_t)but);
            if (st == (uint8_t)ButtonStatePress && prevButtonStates[but] != (uint8_t)ButtonStatePress)
                listener->handleButton((uint8_t)but, st);
            prevButtonStates[but] = st;
        }
    }
#endif

    FrontendApplicationBase::handleTickEvent();
}

void FrontendApplication::gotoScreenMenuScreenNoTransition()
{
    screenMenuTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenMenuScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenMenuTransitionCallback;
}

void FrontendApplication::gotoScreenMenuScreenNoTransitionImpl()
{
    makeTransition<ScreenMenuView, ScreenMenuPresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreenDevicesScreenNoTransition()
{
    screenDevicesTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenDevicesScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenDevicesTransitionCallback;
}

void FrontendApplication::gotoScreenDevicesScreenNoTransitionImpl()
{
    makeTransition<screen_devicesView, screen_devicesPresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreenMenuConnectionScreenNoTransition()
{
    screenMenuConnectionTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenMenuConnectionScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenMenuConnectionTransitionCallback;
}

void FrontendApplication::gotoScreenMenuConnectionScreenNoTransitionImpl()
{
    makeTransition<ScreenMenu_ConnectionView, ScreenMenu_ConnectionPresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreenMenuConfigScreenNoTransition()
{
    screenMenuConfigTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenMenuConfigScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenMenuConfigTransitionCallback;
}

void FrontendApplication::gotoScreenMenuConfigScreenNoTransitionImpl()
{
    makeTransition<ScreenMenu_ConfigView, ScreenMenu_ConfigPresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreenMenuJurnalScreenNoTransition()
{
    screenMenuJurnalTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenMenuJurnalScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenMenuJurnalTransitionCallback;
}

void FrontendApplication::gotoScreenMenuJurnalScreenNoTransitionImpl()
{
    makeTransition<ScreenMenu_jurnalView, ScreenMenu_jurnalPresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreenMenuMcuDetailsScreenNoTransition()
{
    screenMenuMcuDetailsTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenMenuMcuDetailsScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenMenuMcuDetailsTransitionCallback;
}

void FrontendApplication::gotoScreenMenuMcuDetailsScreenNoTransitionImpl()
{
    makeTransition<ScreenMenu_MCU_DetailsView, ScreenMenu_MCU_DetailsPresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}

void FrontendApplication::gotoScreenBlockZoneScreenNoTransition()
{
    screenBlockZoneTransitionCallback = Callback<FrontendApplication>(this, &FrontendApplication::gotoScreenBlockZoneScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &screenBlockZoneTransitionCallback;
}

void FrontendApplication::gotoScreenBlockZoneScreenNoTransitionImpl()
{
    makeTransition<ScreenBlockZoneView, ScreenBlockZonePresenter, NoTransition, Model>(&currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
}
