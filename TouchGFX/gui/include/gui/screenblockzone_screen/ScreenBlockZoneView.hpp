#ifndef SCREENBLOCKZONEVIEW_HPP
#define SCREENBLOCKZONEVIEW_HPP

#include <gui_generated/screenblockzone_screen/ScreenBlockZoneViewBase.hpp>
#include <gui/screenblockzone_screen/ScreenBlockZonePresenter.hpp>

class ScreenBlockZoneView : public ScreenBlockZoneViewBase
{
public:
    ScreenBlockZoneView();
    virtual ~ScreenBlockZoneView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

#ifndef SIMULATOR
    void refreshZoneUi();
    void nextActiveZone();
    void prevActiveZone();
    void cycleSelectedZoneMode();
    uint8_t hasActiveZones() const;
#endif

protected:
#ifndef SIMULATOR
    static const uint8_t MAX_ACTIVE_ZONES = ZONE_NUMBER;

    uint8_t activeZoneIdx_[MAX_ACTIVE_ZONES];
    uint8_t activeZoneCount_;
    uint8_t selectedPos_;
#endif
};

#endif // SCREENBLOCKZONEVIEW_HPP
