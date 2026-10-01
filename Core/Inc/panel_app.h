#ifndef INC_PANEL_APP_H_
#define INC_PANEL_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

void PanelApp_Init(void);
void PanelApp_Timer1ms(void);
void PanelApp_Timer10ms(void);
void PanelApp_WireTouchGfx(void);
void PanelApp_Rs485Init(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_PANEL_APP_H_ */
