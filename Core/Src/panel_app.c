#include "panel_app.h"

#include "button.h"

#include "led.h"

#include "beeper.h"

#include "rtc_cache.h"

#include "rs_panel_endpoint.h"

#include "device_config.h"

#include "backend.h"
#include "menu_ui.h"
#include "main.h"



extern PPKYCfg PPKYConfig;



void PanelApp_Init(void)

{

	PPKYConfig.beep = 1u;

	RtcCache_Refresh();

	Button_Init();

	Led_Init();

	Beeper_Init();

	Beeper_SoundOnOff(true);

	PanelApp_WireTouchGfx();
	PanelApp_Rs485Init();

	Led_Set(LED_POWER, 1u);

	Led_Set(LED_NORM, 1u);

}



void PanelApp_Timer1ms(void)
{
	MenuConfig_Process1ms(HAL_GetTick());
}



void PanelApp_Timer10ms(void)

{

	static uint8_t rtc_div;
	static uint16_t s_app_wd_ticks = 0u;
	static uint8_t s_app_wd_done = 0u;

	Button_Process();
	RsPanelEndpoint_Timer10ms();

	Beeper_Process();

	Led_Process();

	/* 300 * 10 мс = 3 с */
	if (s_app_wd_done == 0u) {
		if (s_app_wd_ticks < 300u) {
			s_app_wd_ticks++;
		} else {
			s_app_wd_done = 1u;
			App_WriteProgramWatchdog();
		}
	}

	if (++rtc_div >= 100u) {

		rtc_div = 0u;

		RtcCache_Tick1s();

	}

}

void PanelApp_Rs485Init(void)
{
	RsPanelEndpoint_Init();
}

