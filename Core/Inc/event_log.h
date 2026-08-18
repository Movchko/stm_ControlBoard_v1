#ifndef INC_EVENT_LOG_H_
#define INC_EVENT_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void EventLog_LogSoundToggle(uint8_t enabled, uint8_t source);
void EventLog_LogFireModeChange(uint8_t mode, uint8_t source);

#ifdef __cplusplus
}
#endif

#endif /* INC_EVENT_LOG_H_ */
