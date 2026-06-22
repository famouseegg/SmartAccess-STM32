#include "serial_comm.h"

#include <string.h>

#include "FreeRTOS.h"
#include "SerialTransfer.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "queue.h"
#include "smart_lock.pb.h"
#include "task.h"

#define PROTO_BUFFER_SIZE 128

QueueHandle_t smartLockTxQueueHandle = NULL;
SerialTransfer myTransfer;

uint8_t g_uart_rx_byte = 0;
extern "C" void SerialTransfer_PutRxByte(uint8_t byte);

extern "C" {
extern UART_HandleTypeDef huart2;
extern QueueHandle_t queueRelayIN;
extern QueueHandle_t queueLed;
extern QueueHandle_t queueLCD;
extern QueueHandle_t queueRegistrationControl;
}

extern "C" void InitSmartLockComms(void) {
  smartLockTxQueueHandle = xQueueCreate(8, sizeof(smart_lock_SmartLockPacket));

  myTransfer.begin(&huart2);
  HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1);
}

extern "C" void StartTask_Tx(void* argument) {
  static uint32_t tx_sequence = 0;
  uint8_t encode_buffer[PROTO_BUFFER_SIZE];
  smart_lock_SmartLockPacket packet;

  while (1) {
    // 阻塞等待，直到其他任務丟入封包實體
    if (xQueueReceive(smartLockTxQueueHandle, &packet, portMAX_DELAY) ==
        pdTRUE) {
      packet.sequence = ++tx_sequence;

      pb_ostream_t stream =
          pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));
      bool encode_status =
          pb_encode(&stream, smart_lock_SmartLockPacket_fields, &packet);

      if (encode_status) {
        size_t bytes_written = stream.bytes_written;

        // 使用 SerialTransfer 打包並發送
        uint16_t sendSize = 0;
        sendSize = myTransfer.txObj(encode_buffer, sendSize, bytes_written);
        myTransfer.sendData(sendSize);
      }
    }
  }
}

/* 接收任務 (Rx Task) */
extern "C" void StartTask_Rx(void* argument) {
  smart_lock_SmartLockPacket rx_packet;
  uint8_t payload_buffer[PROTO_BUFFER_SIZE];

  while (1) {
    // 檢查 SerialTransfer 是否收到完整且校驗正確的封包
    if (myTransfer.available()) {
      uint16_t recSize = 0;
      recSize = myTransfer.rxObj(payload_buffer, recSize, myTransfer.bytesRead);

      if (recSize > 0) {
        pb_istream_t stream = pb_istream_from_buffer(payload_buffer, recSize);
        memset(&rx_packet, 0, sizeof(rx_packet));

        if (pb_decode(&stream, smart_lock_SmartLockPacket_fields, &rx_packet)) {
          // 檢查是否為樹莓派傳來的訊息
          if (rx_packet.which_packet_body ==
              smart_lock_SmartLockPacket_pi_message_tag) {
            smart_lock_PiMessage* pi_msg = &rx_packet.packet_body.pi_message;

            switch (pi_msg->which_body) {
              case smart_lock_PiMessage_unlock_tag: {
                bool open = true;
                char led_color = 'G';
                xQueueSend(queueRelayIN, &open, portMAX_DELAY);
                xQueueSend(queueLed, &led_color, portMAX_DELAY);
                break;
              }
              case smart_lock_PiMessage_control_rgb_led_tag: {
                smart_lock_ControlRgbLedCommand* led_cmd =
                    &pi_msg->body.control_rgb_led;
                char led_color = '0';

                if (led_cmd->red && led_cmd->green)
                  led_color = 'Y';  // 黃
                else if (led_cmd->red)
                  led_color = 'R';  // 紅
                else if (led_cmd->green)
                  led_color = 'G';  // 綠
                else if (led_cmd->blue)
                  led_color = 'B';  // 藍

                if (led_color != '0') {
                  xQueueSend(queueLed, &led_color, portMAX_DELAY);
                }
                break;
              }
              case smart_lock_PiMessage_status_response_tag: {
                LcdDisplay_t displayText;
                char led;
                if (pi_msg->body.status_response.status ==
                    smart_lock_StatusType_STATUS_TYPE_OK) {
                  led = 'G';
                  strcpy(displayText.line1, "successful");
                  strcpy(displayText.line2,
                         pi_msg->body.status_response.message);
                  xQueueSend(queueLed, &led, portMAX_DELAY);
                  xQueueSend(queueLCD, &displayText, portMAX_DELAY);

                } else if (pi_msg->body.status_response.status ==
                           smart_lock_StatusType_STATUS_TYPE_FAILED) {
                  led = 'R';
                  strcpy(displayText.line1, "failed");
                  strcpy(displayText.line2,
                         pi_msg->body.status_response.message);
                  xQueueSend(queueLed, &led, portMAX_DELAY);
                  xQueueSend(queueLCD, &displayText, portMAX_DELAY);
                }
                bool registration = false;
                xQueueSend(queueRegistrationControl, &registration,
                           portMAX_DELAY);
                break;
              }
              default:
                break;
            }
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

extern "C" void SendPINEvent(const char* pwd) {
  smart_lock_SmartLockPacket packet = smart_lock_SmartLockPacket_init_default;
  packet.which_packet_body = smart_lock_SmartLockPacket_stm32_message_tag;
  packet.packet_body.stm32_message.which_body =
      smart_lock_Stm32Message_pin_input_tag;

  strncpy(packet.packet_body.stm32_message.body.pin_input.pin, pwd,
          sizeof(packet.packet_body.stm32_message.body.pin_input.pin) - 1);

  xQueueSend(smartLockTxQueueHandle, &packet, 0);
}

extern "C" void SendPINUpdatedEvent(const char* new_pwd) {
  smart_lock_SmartLockPacket packet = smart_lock_SmartLockPacket_init_default;
  packet.which_packet_body = smart_lock_SmartLockPacket_stm32_message_tag;
  packet.packet_body.stm32_message.which_body =
      smart_lock_Stm32Message_pin_updated_tag;

  strncpy(packet.packet_body.stm32_message.body.pin_updated.pin, new_pwd,
          sizeof(packet.packet_body.stm32_message.body.pin_updated.pin) - 1);

  xQueueSend(smartLockTxQueueHandle, &packet, 0);
}

extern "C" void SendRfidEvent(const uint8_t* uid_data, uint16_t uid_len) {
  smart_lock_SmartLockPacket packet = smart_lock_SmartLockPacket_init_default;
  packet.which_packet_body = smart_lock_SmartLockPacket_stm32_message_tag;
  packet.packet_body.stm32_message.which_body =
      smart_lock_Stm32Message_rfid_scanned_tag;

  uint16_t max_bytes_size =
      sizeof(packet.packet_body.stm32_message.body.rfid_scanned.uid.bytes);
  if (uid_len > max_bytes_size) uid_len = max_bytes_size;

  packet.packet_body.stm32_message.body.rfid_scanned.uid.size = uid_len;
  memcpy(packet.packet_body.stm32_message.body.rfid_scanned.uid.bytes, uid_data,
         uid_len);

  xQueueSend(smartLockTxQueueHandle, &packet, 0);
}

extern "C" void SendRFIDRegistrationEvent(const uint8_t* uid_data,
                                          uint16_t uid_len) {
  smart_lock_SmartLockPacket packet = smart_lock_SmartLockPacket_init_default;
  packet.which_packet_body = smart_lock_SmartLockPacket_stm32_message_tag;
  packet.packet_body.stm32_message.which_body =
      smart_lock_Stm32Message_rfid_registration_tag;

  uint16_t max_bytes_size =
      sizeof(packet.packet_body.stm32_message.body.rfid_registration.uid.bytes);
  if (uid_len > max_bytes_size) uid_len = max_bytes_size;

  packet.packet_body.stm32_message.body.rfid_registration.uid.size = uid_len;
  memcpy(packet.packet_body.stm32_message.body.rfid_registration.uid.bytes,
         uid_data, uid_len);

  xQueueSend(smartLockTxQueueHandle, &packet, 0);
}

extern "C" void SendFaceRegistrationEvent(void) {
  smart_lock_SmartLockPacket packet = smart_lock_SmartLockPacket_init_default;
  packet.which_packet_body = smart_lock_SmartLockPacket_stm32_message_tag;
  packet.packet_body.stm32_message.which_body =
      smart_lock_Stm32Message_face_registration_tag;

  xQueueSend(smartLockTxQueueHandle, &packet, 0);
}

extern "C" void SendSmartLockSignalFromISR(
    SmartLockSignal_t signal, BaseType_t* xHigherPriorityTaskWoken) {
  smart_lock_SmartLockPacket packet = smart_lock_SmartLockPacket_init_default;
  packet.which_packet_body = smart_lock_SmartLockPacket_stm32_message_tag;

  switch (signal) {
    case SIGNAL_IR_TRIGGERED:
      packet.packet_body.stm32_message.which_body =
          smart_lock_Stm32Message_ir_triggered_tag;
      break;
    case SIGNAL_SYSTEM_RESET:
      packet.packet_body.stm32_message.which_body =
          smart_lock_Stm32Message_system_reset_tag;
      break;
    default:
      return;
  }

  xQueueSendFromISR(smartLockTxQueueHandle, &packet, xHigherPriorityTaskWoken);
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
  if (huart->Instance == USART2) {
    // 把硬體剛收到的這一個 byte 塞進環形緩衝區
    SerialTransfer_PutRxByte(g_uart_rx_byte);

    // 再次呼叫接收中斷，準備接收下一個字元
    HAL_UART_Receive_IT(huart, &g_uart_rx_byte, 1);
  }
}
