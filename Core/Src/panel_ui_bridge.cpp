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
        if (MenuUi_IsMainScreenActive() != 0u) {
            MenuUi_SetConfigSession(0u);
            break;
        }
        MenuUi_SetMainScreenActive(1u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotomainscreenScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_ROOT:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoScreenMenuScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_DEVICES:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoScreenDevicesScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_CONNECTION:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoScreenMenuConnectionScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_CONFIG:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(1u);
        panel_ui_app().gotoScreenMenuConfigScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_BLOCK_ZONE:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoScreenBlockZoneScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_JOURNAL:
    case RS_PANEL_SCREEN_MENU_JOURNAL_DETAIL:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoScreenMenuJurnalScreenNoTransition();
        break;
    case RS_PANEL_SCREEN_MENU_DEVICE_DETAIL:
        MenuUi_SetMainScreenActive(0u);
        MenuUi_SetConfigSession(0u);
        panel_ui_app().gotoScreenMenuMcuDetailsScreenNoTransition();
        break;
    default:
        break;
    }
}

extern "C" void PanelUiBridge_SetFireStatus(uint8_t active,
                                            uint8_t mode,
                                            uint8_t remaining_s,
                                            uint8_t n_zones,
                                            char (*zone_names)[ZONE_NAME_SIZE + 1])
{
    Model& model = FrontendHeap::getInstance().model;
    model.setFireStatusFromApp(active != 0u,
                                mode,
                                0xFFu,
                                remaining_s,
                                n_zones,
                                zone_names);

    /* TouchGFX uses Fire_IsActive()/Fire_IsStartAllHoldActive() for priority
     * and forced main-screen switch. На панели реальную пожарную логику
     * заменяем этими RS-driven флагами. */
    Fire_NotifyUiStatus(active, mode, remaining_s, n_zones);
    if (MenuUi_IsMainScreenActive() != 0u) {
        ModelListener* listener = model.getModelListener();
        if (listener != nullptr) {
            listener->onFireStatusChanged(model.getFireActive(),
                                         model.getFireMode(),
                                         0xFFu,
                                         model.getFireRemaining(),
                                         model.getFireZoneNameCount(),
                                         model.getFireZoneNames());
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
