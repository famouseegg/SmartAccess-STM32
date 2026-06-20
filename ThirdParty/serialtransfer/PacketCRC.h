#pragma once
#include <stdio.h>

#include "stm32f4xx_hal.h"

class PacketCRC {
 public:  // <<---------------------------------------//public
  uint8_t poly = 0;

  PacketCRC(const uint8_t& polynomial = 0x9B, const uint8_t& crcLen = 8) {
    poly = polynomial;
    crcLen_ = crcLen;
    tableLen_ = (1 << crcLen);
    if (tableLen_ > 256) tableLen_ = 256;

    generateTable();
  }

  void generateTable() {
    for (uint16_t i = 0; i < tableLen_; ++i) {
      int curr = i;

      for (int j = 0; j < 8; ++j) {
        if ((curr & 0x80) != 0)
          curr = (curr << 1) ^ (int)poly;
        else
          curr <<= 1;
      }

      csTable[i] = (uint8_t)curr;
    }
  }

  void printTable(UART_HandleTypeDef* debugPort = NULL) {
    if (debugPort == NULL) return;

    char buf[16];
    for (uint16_t i = 0; i < tableLen_; i++) {
      int len = sprintf(buf, "%02X ", csTable[i]);
      HAL_UART_Transmit(debugPort, (uint8_t*)buf, len, 50);

      if ((i + 1) % 16 == 0) {
        HAL_UART_Transmit(debugPort, (uint8_t*)"\r\n", 2, 50);
      }
    }
  }

  uint8_t calculate(const uint8_t& val) {
    if (val < tableLen_) return csTable[val];
    return 0;
  }

  uint8_t calculate(uint8_t arr[], uint8_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) crc = csTable[crc ^ arr[i]];

    return crc;
  }

 private:  // <<---------------------------------------//private
  uint16_t tableLen_;
  uint8_t crcLen_;
  uint8_t csTable[256];
};

extern PacketCRC crc;
