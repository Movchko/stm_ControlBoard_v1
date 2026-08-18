#ifndef SCREENMENU_JURNALVIEW_HPP
#define SCREENMENU_JURNALVIEW_HPP

#include <gui_generated/screenmenu_jurnal_screen/ScreenMenu_jurnalViewBase.hpp>
#include <gui/screenmenu_jurnal_screen/ScreenMenu_jurnalPresenter.hpp>

class ScreenMenu_jurnalView : public ScreenMenu_jurnalViewBase
{
public:
    ScreenMenu_jurnalView();
    virtual ~ScreenMenu_jurnalView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

#ifndef SIMULATOR
    void refreshJournalUi();
    void nextRecord();     /* DOWN: новее */
    void prevRecord();     /* UP:   старее */
    void jumpToNewest();   /* ENTER */
#endif
protected:
#ifndef SIMULATOR
    void renderCurrent();
    bool loadLogical(uint32_t logical_index);
    bool stepValid(int direction); /* -1 older, +1 newer */

    uint32_t recordCount = 0u;
    uint32_t logicalIndex = 0u; /* 0 = oldest among count */
    uint8_t hasValid = 0u;
#endif
};

#endif // SCREENMENU_JURNALVIEW_HPP
