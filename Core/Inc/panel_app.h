#ifndef INC_PANEL_APP_H_
#define INC_PANEL_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Версия приложения панели. ACTIVITY.fw_ver и ответ на 159 берут это число.
 * Бутлоадер отвечает отдельно: "boot=2". */
#define PANEL_APP_VERSION_U32 2u

void PanelApp_Init(void);
void PanelApp_Timer1ms(void);
void PanelApp_Timer10ms(void);
void PanelApp_WireTouchGfx(void);
void PanelApp_Rs485Init(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_PANEL_APP_H_ */
