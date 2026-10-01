#ifndef INC_UPD_H_
#define INC_UPD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Числовая версия приложения (CAPS.fw_ver). Строка — GetAppVersion() из backend.h. */
uint32_t GetAppVersionU32(void);

/* Запрос входа в бутлоадер: handoff в TAMP, reset из PanelUpd_Timer10ms. */
void PanelUpd_RequestEnterBootloader(uint8_t rs_addr);
void PanelUpd_Timer10ms(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_UPD_H_ */
