#ifndef MODEL_HPP
#define MODEL_HPP

#ifndef SIMULATOR
#include <main.h>
#include "device_config.h"
#define WARNING_TITLE_LEN 24
#endif


class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    /** Текущий слушатель (привязанный презентер активного экрана) */
    ModelListener* getModelListener() { return modelListener; }

    void tick();

    /** Состояние параметра «звук включен/выключен» в модели (уведомление UI только при смене). */
    void setSoundOn(bool on);
    bool getSoundOn() const { return soundOn; }

#ifndef SIMULATOR
    /** Колбэк: приложение узнаёт, что звук включён/выключен (сохранение — в приложении) */
    typedef void (*SoundToggledCallback)(bool soundOn);
    void setSoundToggledCallback(SoundToggledCallback cb) { soundToggledCallback = cb; }
    void notifySoundToggled(bool soundOn);

    /* Вызов из приложения ППКУ: обновить состояние пожара для UI.
     * active      - true, если пожар активен
     * zone        - номер зоны (0..ZONE_NUMBER-1) или 0xFF
     * remaining_s - оставшееся время до автозапуска тушения (сек), 0 если таймера нет
     * nZoneNames / zoneNames - список имён активных зон (ротация и пауза 3 с — в mainscreenView)
     */
    void setFireStatusFromApp(bool active, uint8_t mode, uint8_t zone, uint8_t remaining_s, uint8_t nZoneNames,
			      char (*zoneNames)[ZONE_NAME_SIZE + 1]);

    /* Обновление предупреждений (неисправностей) для главного экрана. */
    void setWarningStatusFromApp(bool active, uint8_t nItems, char (*bigTitles)[WARNING_TITLE_LEN],
				 char (*details)[ZONE_NAME_SIZE + 1]);

    bool getWarningActive() const { return warningActive; }
    uint8_t getWarningCount() const { return warningCount; }
    const char (*getWarningBigTitles() const)[WARNING_TITLE_LEN] { return warningBigTitles; }
    const char (*getWarningDetails() const)[ZONE_NAME_SIZE + 1] { return warningDetails; }

    bool getFireActive() const { return fireActive; }
    uint8_t getFireMode() const { return fireMode; }
    uint8_t getFireRemaining() const { return fireRemaining; }
    uint8_t getFireZoneNameCount() const { return fireZoneNameCount; }
    char (*getFireZoneNames())[ZONE_NAME_SIZE + 1] { return fireZoneNames; }
#endif

protected:
    ModelListener* modelListener;

    bool soundOn;

#ifndef SIMULATOR
    SoundToggledCallback soundToggledCallback;

    bool fireActive = false;
    uint8_t fireMode = 0u;
    uint8_t fireZone = 0xFF;
    uint8_t fireRemaining = 0;
    uint8_t fireZoneNameCount = 0;
    char fireZoneNames[16][ZONE_NAME_SIZE + 1];

    bool warningActive = false;
    uint8_t warningCount = 0;
    char warningBigTitles[16][WARNING_TITLE_LEN];
    char warningDetails[16][ZONE_NAME_SIZE + 1];
#endif
};

#endif // MODEL_HPP
