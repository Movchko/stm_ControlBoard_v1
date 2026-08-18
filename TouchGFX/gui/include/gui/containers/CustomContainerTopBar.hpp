#ifndef CUSTOMCONTAINERTOPBAR_HPP
#define CUSTOMCONTAINERTOPBAR_HPP

#include <gui_generated/containers/CustomContainerTopBarBase.hpp>
#include <touchgfx/widgets/Box.hpp>

class CustomContainerTopBar : public CustomContainerTopBarBase
{
public:
    CustomContainerTopBar();
    virtual ~CustomContainerTopBar() {}

    virtual void initialize();

    /** imageMute: показать при выключенном звуке. */
    void setMuteVisible(bool visible);
    /** imageWifi: показать при активном TCP-подключении хоста. */
    void setWifiVisible(bool visible);

protected:
    /** Непрозрачный фон под imageWifi/imageMute (BW-битмапы прозрачны). */
    touchgfx::Box iconsBackground;
};

#endif // CUSTOMCONTAINERTOPBAR_HPP
