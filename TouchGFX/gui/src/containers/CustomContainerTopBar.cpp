#include <gui/containers/CustomContainerTopBar.hpp>
#include <touchgfx/Color.hpp>

CustomContainerTopBar::CustomContainerTopBar()
{
    /* BW-иконки пишут только «белые» пиксели; без подложки в framebuffer
     * остаётся мусор при show/hide. Фон — справа под wifi/mute.
     * insert(previous, d) вставляет d ПОСЛЕ previous; previous==0 → в начало
     * (под всеми детьми), иначе чёрный box перекрывал imageMute. */
    iconsBackground.setPosition(99, 0, 29, 15);
    iconsBackground.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    insert(0, iconsBackground);
}

void CustomContainerTopBar::initialize()
{
    CustomContainerTopBarBase::initialize();
}

void CustomContainerTopBar::setMuteVisible(bool visible)
{
    if (imageMute.isVisible() == visible) {
        return;
    }
    /* Сначала пометить область (пока виджет ещё visible при hide), затем сменить флаг. */
    imageMute.invalidate();
    imageMute.setVisible(visible);
    iconsBackground.invalidate();
    invalidate();
}

void CustomContainerTopBar::setWifiVisible(bool visible)
{
    if (imageWifi.isVisible() == visible) {
        return;
    }
    imageWifi.invalidate();
    imageWifi.setVisible(visible);
    iconsBackground.invalidate();
    invalidate();
}
