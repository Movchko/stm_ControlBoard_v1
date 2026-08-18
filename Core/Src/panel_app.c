#include "panel_app.h"

#include "button.h"

#include "led.h"

#include "beeper.h"

#include "rtc_cache.h"

#include "device_config.h"



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

	Led_Set(LED_POWER, 1u);

	Led_Set(LED_NORM, 1u);

}



void PanelApp_Timer1ms(void)

{

	static uint8_t div10;


/*
	if (++div10 >= 10u) {

		div10 = 0u;

		PanelApp_Timer10ms();

	}
*/
}



void PanelApp_Timer10ms(void)

{

	static uint8_t rtc_div;

	Button_Process();

	Beeper_Process();

	Led_Process();

	if (++rtc_div >= 100u) {

		rtc_div = 0u;

		RtcCache_Tick1s();

	}

}

