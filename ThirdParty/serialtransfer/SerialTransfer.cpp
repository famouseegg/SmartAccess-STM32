#include "SerialTransfer.h"

#define ST_RX_BUFFER_SIZE 256

typedef struct {
  uint8_t buffer[ST_RX_BUFFER_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
} ST_RingBuffer_t;

static ST_RingBuffer_t st_rx_fifo = {{0}, 0, 0};

extern "C" void SerialTransfer_PutRxByte(uint8_t byte) {
  uint16_t next_head = (st_rx_fifo.head + 1) & (ST_RX_BUFFER_SIZE - 1);

  if (next_head != st_rx_fifo.tail) {
    st_rx_fifo.buffer[st_rx_fifo.head] = byte;
    st_rx_fifo.head = next_head;
  }
}
/*
 void SerialTransfer::begin(UART_HandleTypeDef *_port, configST configs)
 Description:
 ------------
  * Advanced initializer for the SerialTransfer Class
 Inputs:
 -------
  * const UART_HandleTypeDef *_port - UART handle for communication
  * const configST configs - Struct that holds config
  values for all possible initialization parameters
 Return:
 -------
  * void
*/
inline bool available() { return (st_rx_fifo.head != st_rx_fifo.tail); }

inline uint8_t read() {
  if (st_rx_fifo.head == st_rx_fifo.tail) return 0;

  uint8_t byte = st_rx_fifo.buffer[st_rx_fifo.tail];
  st_rx_fifo.tail = (st_rx_fifo.tail + 1) & (ST_RX_BUFFER_SIZE - 1);
  return byte;
}

void SerialTransfer::begin(UART_HandleTypeDef* _port, const configST configs) {
  port = _port;
  packet.begin(configs);
}

/*
 void SerialTransfer::begin(UART_HandleTypeDef *_port, const bool _debug,
 UART_HandleTypeDef *_debugPort) Description:
 ------------
  * Simple initializer for the SerialTransfer Class
 Inputs:
 -------
  * const UART_HandleTypeDef *_port - UART handle for communication
  * const bool _debug - Whether or not to print error messages
  * const UART_HandleTypeDef *_debugPort - UART handle for debug output
 Return:
 -------
  * void
*/
void SerialTransfer::begin(UART_HandleTypeDef* _port, const bool _debug,
                           UART_HandleTypeDef* _debugPort, uint32_t _timeout) {
  port = _port;
  timeout = _timeout;
  packet.begin(_debug, _debugPort, _timeout);
}

/*
 uint8_t SerialTransfer::sendData(const uint16_t &messageLen, const uint8_t
 packetID) Description:
 ------------
  * Send a specified number of bytes in packetized form
 Inputs:
 -------
  * const uint16_t &messageLen - Number of values in txBuff
  to send as the payload in the next packet
  * const uint8_t packetID - The packet 8-bit identifier
 Return:
 -------
  * uint8_t numBytesIncl - Number of payload bytes included in packet
*/
uint8_t SerialTransfer::sendData(const uint16_t& messageLen,
                                 const uint8_t packetID) {
  uint8_t numBytesIncl;

  numBytesIncl = packet.constructPacket(messageLen, packetID);
  HAL_UART_Transmit(port, packet.preamble, PREAMBLE_SIZE, timeout);
  HAL_UART_Transmit(port, packet.txBuff, numBytesIncl, timeout);
  HAL_UART_Transmit(port, packet.postamble, POSTAMBLE_SIZE, timeout);

  return numBytesIncl;
}

/*
 uint8_t SerialTransfer::available()
 Description:
 ------------
  * Parses incoming serial data, analyzes packet contents,
  and reports errors/successful packet reception
 Inputs:
 -------
  * void
 Return:
 -------
  * uint8_t bytesRead - Num bytes in RX buffer
*/
uint8_t SerialTransfer::available() {
  bool valid = false;
  uint8_t recChar = 0xFF;
  bytesRead = 0;

  if (::available()) {
    valid = true;

    while (::available()) {
      recChar = ::read();

      bytesRead = packet.parse(recChar, valid);
      status = packet.status;

      if (status != CONTINUE) {
        if (status <= 0) reset();

        break;
      }

      if (status == CONTINUE) {
        bytesRead = 0;
      }
    }
  }

  return bytesRead;
}

/*
 bool SerialTransfer::tick()
 Description:
 ------------
  * Checks to see if any packets have been fully parsed. This
  is basically a wrapper around the method "available()" and
  is used primarily in conjunction with callbacks
 Inputs:
 -------
  * void
 Return:
 -------
  * bool - Whether or not a full packet has been parsed
*/
bool SerialTransfer::tick() {
  if (available()) return true;

  return false;
}

/*
 uint8_t SerialTransfer::currentPacketID()
 Description:
 ------------
  * Returns the ID of the last parsed packet
 Inputs:
 -------
  * void
 Return:
 -------
  * uint8_t - ID of the last parsed packet
*/
uint8_t SerialTransfer::currentPacketID() { return packet.currentPacketID(); }

/*
 void SerialTransfer::reset()
 Description:
 ------------
  * Clears out the tx, and rx buffers, plus resets
  the "bytes read" variable, finite state machine, etc
 Inputs:
 -------
  * void
 Return:
 -------
  * void
*/
void SerialTransfer::reset() {
  packet.reset();
  status = packet.status;
}
