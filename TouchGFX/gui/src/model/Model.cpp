#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <cstring>

#ifndef SIMULATOR
#include "rtc_cache.h"
#include "esp_manager.h"
#endif

Model::Model() : modelListener(0), soundOn(true)
{
#ifndef SIMULATOR
    soundToggledCallback = 0;
#endif
}

void Model::setSoundOn(bool on)
{
	if (soundOn == on) {
		return;
	}
	soundOn = on;
#ifndef SIMULATOR
	if (modelListener) {
		modelListener->onSoundOnChanged(soundOn);
	}
#endif
}

unsigned char pos = 0;
void Model::tick()
{
#ifndef SIMULATOR
	if (modelListener)
	{
		// Обновляем время на UI только раз в секунду (или когда реально поменялась секунда),
		// чтобы не делать тяжёлые чтения RTC и invalidate() на каждом touchgfx tick.
		static uint32_t lastRtcUpdateMs = 0;
		static uint8_t lastSec = 0xFF;

		uint32_t nowMs = HAL_GetTick();
		if ((nowMs - lastRtcUpdateMs) >= 1000u) {
			lastRtcUpdateMs = nowMs;

			RTC_TimeTypeDef sTime;
			RTC_DateTypeDef sDate;
			if (RtcCache_GetBin(&sTime, &sDate)) {
				uint8_t sec = (uint8_t)sTime.Seconds;
				if (sec != lastSec) {
					lastSec = sec;
					modelListener->setDateTime(
						(uint8_t)sTime.Hours,
						(uint8_t)sTime.Minutes,
						sec,
						(uint8_t)sDate.Date,
						(uint8_t)sDate.Month,
						(uint8_t)sDate.Year);
				}
			}
		}

		/* Проксируем актуальный статус пожара на активный экран каждый tick */
		modelListener->onFireStatusChanged(fireActive, fireMode, fireZone, fireRemaining,
						     fireZoneNameCount, fireZoneNames);
		modelListener->onWarningStatusChanged(warningActive, warningCount, warningBigTitles, warningDetails);
#ifndef SIMULATOR
		{
			static uint8_t lastWifiIconVis = 0xFFu;
			static uint8_t lastSessionActive = 0xFFu;
			static uint8_t lastLinked = 0xFFu;
			static uint32_t lastWifiIconPollMs = 0u;
			uint8_t sessionActive = EspManager_IsWifiSessionActive();
			uint8_t linked = EspManager_IsLinkActive();
			uint8_t pollNow = 0u;

			if (sessionActive != lastSessionActive) {
				lastSessionActive = sessionActive;
				pollNow = 1u;
			}
			if (linked != lastLinked) {
				lastLinked = linked;
				pollNow = 1u;
				lastWifiIconVis = 0xFFu;
			}
			if (pollNow == 0u) {
				if (linked != 0u || sessionActive == 0u) {
					pollNow = 1u;
				} else if ((nowMs - lastWifiIconPollMs) >= 500u) {
					pollNow = 1u;
				}
			}

			if (pollNow != 0u) {
				lastWifiIconPollMs = nowMs;
				uint8_t wifiIconVis = EspManager_IsWifiIconVisible(nowMs);
				if (wifiIconVis != lastWifiIconVis) {
					lastWifiIconVis = wifiIconVis;
					modelListener->onWifiLinkChanged(wifiIconVis != 0u);
				}
			}
		}
#endif
		modelListener->onAppTick();
	}

#endif
}

#ifndef SIMULATOR
void Model::notifySoundToggled(bool soundOn)
{
    if (soundToggledCallback)
        soundToggledCallback(soundOn);
}

void Model::setFireStatusFromApp(bool active, uint8_t mode, uint8_t zone, uint8_t remaining_s, uint8_t nZoneNames,
				 char (*zoneNames)[ZONE_NAME_SIZE + 1])
{
	fireActive = active;
	fireMode = mode;
	fireZone = zone;
	fireRemaining = remaining_s;
	if (nZoneNames > 16u) {
		nZoneNames = 16u;
	}
	fireZoneNameCount = nZoneNames;
	if (nZoneNames == 0u) {
		std::memset(fireZoneNames, 0, sizeof(fireZoneNames));
		/* Не выходим раньше: active/mode/remaining уже обновлены (сброс таймера ПУСК ОБЩИЙ). */
		return;
	}
	for (uint8_t i = 0u; i < nZoneNames; i++) {
		std::strncpy(fireZoneNames[i], zoneNames[i], ZONE_NAME_SIZE);
		fireZoneNames[i][ZONE_NAME_SIZE] = '\0';
	}
}

void Model::setWarningStatusFromApp(bool active, uint8_t nItems, char (*bigTitles)[WARNING_TITLE_LEN],
				    char (*details)[ZONE_NAME_SIZE + 1])
{
	warningActive = active;
	if (nItems > 16u) {
		nItems = 16u;
	}
	warningCount = nItems;
	if (!active || nItems == 0u) {
		std::memset(warningBigTitles, 0, sizeof(warningBigTitles));
		std::memset(warningDetails, 0, sizeof(warningDetails));
		return;
	}
	for (uint8_t i = 0u; i < nItems; i++) {
		std::strncpy(warningBigTitles[i], bigTitles[i], WARNING_TITLE_LEN - 1u);
		warningBigTitles[i][WARNING_TITLE_LEN - 1u] = '\0';
		std::strncpy(warningDetails[i], details[i], ZONE_NAME_SIZE);
		warningDetails[i][ZONE_NAME_SIZE] = '\0';
	}
}
#endif
