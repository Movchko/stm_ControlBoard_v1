#include "panel_ui_bridge.h"

#include <gui/common/FrontendHeap.hpp>
#include <gui/model/ModelListener.hpp>
#include "rs_panel_protocol.h"

extern "C" {
#include "menu_ui.h"
}

static FrontendApplication& panel_ui_app()
{
    return FrontendHeap::getInstance().app;
}

extern "C" void Fire_NotifyUiStatus(uint8_t ui_active,
                                    uint8_t mode,
                                    uint8_t remaining_s,
                                    uint8_t n_zones);

extern "C" void PanelUiBridge_GotoScreen(uint16_t screen_id, uint8_t action)
{
    if (action == 1u || action == 2u) {
        return;
    }

    switch (screen_id) {
    case RS_PANEL_SCREEN_LOGO:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoscreen_logoScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MAIN:
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(0u);
        /* main=1 ставит только реальный переход (не rs_queue_nav), иначе сюда
         * пришли бы с main=1 ещё на экране меню и переход бы пропустился. */
        if (MenuUi_IsMainScreenActive() != 0u) {
            break;
        }
        MenuUi_SetMainScreenActive(1u);
        panel_ui_app().gotomainscreenScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_ROOT:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_ROOT);
        panel_ui_app().gotoScreenMenuScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_DEVICES:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_DEVICES);
        panel_ui_app().gotoScreenDevicesScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_CONNECTION:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_CONNECTION);
        panel_ui_app().gotoScreenMenuConnectionScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_CONFIG:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(1u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_CONFIG);
        panel_ui_app().gotoScreenMenuConfigScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_BLOCK_ZONE:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_BLOCK_ZONE);
        panel_ui_app().gotoScreenBlockZoneScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_JOURNAL:
    case RS_PANEL_SCREEN_MENU_JOURNAL_DETAIL:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(screen_id);
        panel_ui_app().gotoScreenMenuJurnalScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_DEVICE_DETAIL:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_DEVICE_DETAIL);
        panel_ui_app().gotoScreenMenuMcuDetailsScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_TEST_SELECT:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_TEST_SELECT);
        panel_ui_app().gotoTestSelectScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_TEST_LAMPS:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        MenuUi_SetMenuSessionScreen(RS_PANEL_SCREEN_MENU_TEST_LAMPS);
        panel_ui_app().gotoTestScreenNoTransition();
        break;
    default:
        break;
    }
}

extern "C" void PanelUiBridge_SetFireStatus(uint8_t active,
                                            uint8_t mode,
                                            uint8_t remaining_s,
                                            uint8_t n_zones,
                                            char (*zone_names)[ZONE_NAME_SIZE + 1],
                                            const uint8_t *zone_modes,
                                            const uint8_t *zone_remaining)
{
    Model& model = FrontendHeap::getInstance().model;
    model.setFireStatusFromApp(active != 0u,
                                mode,
                                0xFFu,
                                remaining_s,
                                n_zones,
                                zone_names,
                                zone_modes,
                                zone_remaining);

    /* TouchGFX uses Fire_IsActive()/Fire_IsStartAllHoldActive() for priority
     * and forced main-screen switch. На панели реальную пожарную логику
     * заменяем этими RS-driven флагами. */
    const uint8_t is_hold_idle =
        (active != 0u && mode == 1u && n_zones == 0u) ? 1u : 0u;

    Fire_NotifyUiStatus(active, mode, remaining_s, n_zones);
    /* Удержание ПУСК ОБЩИЙ: обновлять главный экран даже до UI_NAV с меню. */
    if (MenuUi_IsMainScreenActive() != 0u || is_hold_idle != 0u) {
        ModelListener* listener = model.getModelListener();
        if (listener != nullptr) {
            listener->onFireStatusChanged(model.getFireActive(),
                                         model.getFireMode(),
                                         0xFFu,
                                         model.getFireRemaining(),
                                         model.getFireZoneNameCount(),
                                         model.getFireZoneNames(),
                                         model.getFireZoneModes(),
                                         model.getFireZoneRemaining());
        }
    }
}

extern "C" void PanelUiBridge_SetWarningStatus(uint8_t active,
                                               uint8_t n_items,
                                               char (*titles)[24],
                                               char (*details)[ZONE_NAME_SIZE + 1])
{
    Model& model = FrontendHeap::getInstance().model;
    model.setWarningStatusFromApp(active != 0u,
                                  n_items,
                                  titles,
                                  details);
    if (MenuUi_IsMainScreenActive() != 0u) {
        ModelListener* listener = model.getModelListener();
        if (listener != nullptr) {
            listener->onWarningStatusChanged(model.getWarningActive(),
                                             model.getWarningCount(),
                                             const_cast<char (*)[WARNING_TITLE_LEN]>(model.getWarningBigTitles()),
                                             const_cast<char (*)[ZONE_NAME_SIZE + 1]>(model.getWarningDetails()));
        }
    }
}

extern "C" void PanelUiBridge_StartIndicationTest(void)
{
    MenuUi_RequestIndicationTest();
}

extern "C" void PanelUiBridge_NotifySysReady(void)
{
    Model& model = FrontendHeap::getInstance().model;
    ModelListener* listener = model.getModelListener();
    if (listener != nullptr) {
        listener->onSysReadyChanged();
    }
}
