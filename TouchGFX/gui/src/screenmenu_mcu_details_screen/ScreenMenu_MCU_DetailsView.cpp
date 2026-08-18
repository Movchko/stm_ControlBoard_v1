#include <gui/screenmenu_mcu_details_screen/ScreenMenu_MCU_DetailsView.hpp>
#include <touchgfx/Unicode.hpp>
#include <cstring>
#include <cstdio>

#ifndef SIMULATOR
#include "device_config.h"

extern PPKYCfg PPKYConfig;

namespace {
static bool isMcuType(uint8_t d_type)
{
    return (d_type == DEVICE_MCU_IGN_TYPE ||
            d_type == DEVICE_MCU_TC_TYPE ||
            d_type == DEVICE_MCU_K1 ||
            d_type == DEVICE_MCU_K2 ||
            d_type == DEVICE_MCU_K3 ||
            d_type == DEVICE_MCU_KR);
}

static const char* mcuTypeLabel(uint8_t d_type)
{
    switch (d_type) {
    case DEVICE_MCU_TC_TYPE:  return "TC";
    case DEVICE_MCU_IGN_TYPE: return "IGN";
    case DEVICE_MCU_K1:       return "K1";
    case DEVICE_MCU_K2:       return "K2";
    case DEVICE_MCU_K3:       return "K3";
    case DEVICE_MCU_KR:       return "KR";
    default:                  return "?";
    }
}

static void trimZoneName(char* dst, size_t dst_sz, const int8_t* src, size_t src_len)
{
    if (dst == nullptr || dst_sz == 0u || src == nullptr) {
        return;
    }
    size_t n = (src_len < (dst_sz - 1u)) ? src_len : (dst_sz - 1u);
    for (size_t i = 0; i < n; i++) {
        dst[i] = (char)src[i];
    }
    dst[n] = '\0';
    while (n > 0u && (dst[n - 1u] == ' ' || dst[n - 1u] == '\0')) {
        dst[n - 1u] = '\0';
        n--;
    }
}
} // namespace
#endif

ScreenMenu_MCU_DetailsView::ScreenMenu_MCU_DetailsView()
{
}

void ScreenMenu_MCU_DetailsView::setupScreen()
{
    ScreenMenu_MCU_DetailsViewBase::setupScreen();
#ifndef SIMULATOR
    refreshDeviceList();
#endif
}

void ScreenMenu_MCU_DetailsView::tearDownScreen()
{
    ScreenMenu_MCU_DetailsViewBase::tearDownScreen();
}

#ifndef SIMULATOR
void ScreenMenu_MCU_DetailsView::refreshDeviceList()
{
    deviceCount = 0u;
    selectedIndex = 0u;
    for (uint8_t i = 0u; i < 32u; i++) {
        const Device* dev = &PPKYConfig.CfgDevices[i].UId.devId;
        if (isMcuType(dev->d_type)) {
            deviceSlots[deviceCount++] = i;
        }
    }

    if (deviceCount == 0u) {
        Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("МКУ -"),
                          textArea_MCUBuffer, TEXTAREA_MCU_SIZE);
        textArea_MCUBuffer[TEXTAREA_MCU_SIZE - 1] = 0;
        textArea_MCU.setWildcard(textArea_MCUBuffer);
        textArea_MCU.invalidate();
        CustomContainerSrollText_Zone.setText("Нет МКУ в конфигурации");
        CustomContainerSrollText_SN.setText("-");
        return;
    }

    renderSelected();
}

void ScreenMenu_MCU_DetailsView::selectCfgSlot(uint8_t cfg_slot)
{
    if (deviceCount == 0u) {
        return;
    }
    for (uint8_t i = 0u; i < deviceCount; i++) {
        if (deviceSlots[i] == cfg_slot) {
            selectedIndex = i;
            renderSelected();
            return;
        }
    }
}

void ScreenMenu_MCU_DetailsView::renderSelected()
{
    if (deviceCount == 0u) {
        return;
    }

    uint8_t slot = deviceSlots[selectedIndex];
    const MKUCfg* mku = &PPKYConfig.CfgDevices[slot];
    const Device* dev = &mku->UId.devId;

    char mcuLine[32];
    (void)std::snprintf(mcuLine, sizeof(mcuLine), "МКУ-%s %u",
                        mcuTypeLabel(dev->d_type), (unsigned)dev->h_adr);
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(mcuLine),
                      textArea_MCUBuffer, TEXTAREA_MCU_SIZE);
    textArea_MCUBuffer[TEXTAREA_MCU_SIZE - 1] = 0;
    textArea_MCU.setWildcard(textArea_MCUBuffer);
    textArea_MCU.invalidate();

    char zoneName[ZONE_NAME_SIZE + 1] = {0};
    uint8_t zone_idx = (dev->zone == 0u) ? 0u : (uint8_t)(dev->zone - 1u);
    if (zone_idx < ZONE_NUMBER) {
        trimZoneName(zoneName, sizeof(zoneName), PPKYConfig.zone_name[zone_idx], ZONE_NAME_SIZE);
    }
    if (zoneName[0] == '\0') {
        (void)std::snprintf(zoneName, sizeof(zoneName), "Зона %u", (unsigned)dev->zone);
    }
    CustomContainerSrollText_Zone.setText(zoneName);

    char snLine[40];
    (void)std::snprintf(snLine, sizeof(snLine), "%08lX:%08lX:%08lX",
                        (unsigned long)mku->UId.UId0,
                        (unsigned long)mku->UId.UId1,
                        (unsigned long)mku->UId.UId2);
    CustomContainerSrollText_SN.setText(snLine);
}

void ScreenMenu_MCU_DetailsView::nextDevice()
{
    if (deviceCount == 0u) {
        return;
    }
    selectedIndex = (uint8_t)((selectedIndex + 1u) % deviceCount);
    renderSelected();
}

void ScreenMenu_MCU_DetailsView::prevDevice()
{
    if (deviceCount == 0u) {
        return;
    }
    selectedIndex = (uint8_t)((selectedIndex == 0u) ? (deviceCount - 1u) : (selectedIndex - 1u));
    renderSelected();
}

uint8_t ScreenMenu_MCU_DetailsView::getSelectedCfgSlot() const
{
    if (deviceCount == 0u) {
        return 0xFFu;
    }
    return deviceSlots[selectedIndex];
}
#endif
