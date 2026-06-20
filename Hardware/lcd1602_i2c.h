#ifndef __LCD1602_I2C_H
#define __LCD1602_I2C_H

#include "stm32f4xx_hal.h"

void LCD_Init(uint8_t lcd_addr);
void LCD_SendCommand(uint8_t lcd_addr, uint8_t cmd);
void LCD_SendData(uint8_t lcd_addr, uint8_t data);
void LCD_SendString(uint8_t lcd_addr, char* str);
void I2C_Scan(void);

#endif
