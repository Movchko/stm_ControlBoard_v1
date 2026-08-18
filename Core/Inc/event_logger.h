#ifndef INC_EVENT_LOGGER_H_
#define INC_EVENT_LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define EVENT_LOG_RECORD_SIZE 32u

#if defined(__GNUC__)
#pragma pack(push, 1)
#endif

typedef struct {
	uint8_t  time[6];
	uint8_t  master_wagon_num;
	uint8_t  reserved;
	uint16_t event_code;
	uint32_t can_header;
	uint8_t  can_data[8];
	uint8_t  additional[8];
	uint16_t checksum;
} EventLogRecord_t;

#if defined(__GNUC__)
#pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif

#endif /* INC_EVENT_LOGGER_H_ */
