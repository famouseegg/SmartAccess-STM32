#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 通訊專用列舉與結構體定義 */
typedef enum {
  SIGNAL_IR_TRIGGERED,  // 紅外線觸發
  SIGNAL_SYSTEM_RESET   // 系統重設
} SmartLockSignal_t;

typedef struct {
  char line1[17];
  char line2[17];
} LcdDisplay_t;

void InitSmartLockComms(void);
void StartTask_Tx(void* argument);
void StartTask_Rx(void* argument);

void SendPINEvent(const char* pwd);
void SendPINUpdatedEvent(const char* new_pwd);
void SendRfidEvent(const uint8_t* uid_data, uint16_t uid_len);
void SendRFIDRegistrationEvent(const uint8_t* uid_data, uint16_t uid_len);
void SendFaceRegistrationEvent(void);
void SendSmartLockSignalFromISR(SmartLockSignal_t signal,
                                BaseType_t* xHigherPriorityTaskWoken);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_COMM_H */