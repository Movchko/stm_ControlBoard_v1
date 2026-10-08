#ifndef PANEL_LINK_MONITOR_H
#define PANEL_LINK_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** После потери ранее установленной связи с ППКУ. */
#define PANEL_LINK_LOST_AFTER_OK_MS  15000u
/** После старта панели ни одного кадра от ППКУ. */
#define PANEL_LINK_NEVER_BOOT_MS     30000u

void PanelLinkMonitor_Init(void);
void PanelLinkMonitor_Timer10ms(void);
/** 1 = показываем неисправность «НЕТ СВЯЗИ». */
uint8_t PanelLinkMonitor_IsLost(void);

#ifdef __cplusplus
}
#endif

#endif /* PANEL_LINK_MONITOR_H */
