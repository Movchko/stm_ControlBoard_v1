#ifndef SCREENMENU_MCU_DETAILSVIEW_HPP
#define SCREENMENU_MCU_DETAILSVIEW_HPP

#include <gui_generated/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsViewBase.hpp>
#include <gui/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsPresenter.hpp>

class ScreenMenu_MCU_DetailsView : public ScreenMenu_MCU_DetailsViewBase
{
public:
    ScreenMenu_MCU_DetailsView();
    virtual ~ScreenMenu_MCU_DetailsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

#ifndef SIMULATOR
    void refreshDeviceList();
    void selectCfgSlot(uint8_t cfg_slot);
    void nextDevice();
    void prevDevice();
    uint8_t getSelectedCfgSlot() const;
#endif
protected:
#ifndef SIMULATOR
    void renderSelected();

    uint8_t selectedIndex = 0u;
    uint8_t deviceSlots[32] = {0u};
    uint8_t deviceCount = 0u;
#endif
};

#endif // SCREENMENU_MCU_DETAILSVIEW_HPP
