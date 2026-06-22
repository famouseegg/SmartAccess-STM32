/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "cmsis_os.h"
#include "gpio.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "RC522.h"
#include "lcd1602_i2c.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "queue.h"
#include "semphr.h"
#include "serial_comm.h"
#include "stdio.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
TaskHandle_t handleKeypad;
TaskHandle_t handleRFID;
TaskHandle_t handleLed;
TaskHandle_t handleLCD;
TaskHandle_t handleTx;
TaskHandle_t handleRx;
TaskHandle_t handleRelayIN;

QueueHandle_t queueKeypad;
QueueHandle_t queueLed;
QueueHandle_t queueLCD;
QueueHandle_t queueRelayIN;
QueueHandle_t queueRelayOUT;
QueueHandle_t queueRFID;
QueueHandle_t queueRFIDRegistration;
QueueHandle_t queueRegistrationControl;

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart2;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void taskKeypad(void* pvParm) {
  static char key_buffer[17] = {0};
  uint16_t COL_PINS[4] = {COL1_Pin, COL2_Pin, COL3_Pin, COL4_Pin};
  uint16_t ROW_PINS[4] = {ROW1_Pin, ROW2_Pin, ROW3_Pin, ROW4_Pin};
  bool isDoorOpen = false;
  bool isRegistration = false;
  bool isUpdation = false;

  char key_matrix_values[4][4] = {{'1', '2', '3', 'A'},
                                  {'4', '5', '6', 'B'},
                                  {'7', '8', '9', 'C'},
                                  {'*', '0', '#', 'D'}};

  HAL_GPIO_WritePin(GPIOF, COL1_Pin | COL2_Pin | COL3_Pin | COL4_Pin,
                    GPIO_PIN_SET);

  while (1) {
    LcdDisplay_t displayText;
    uint8_t isKeyPressed = 0;
    char key = '0';

    for (int c = 0; c < 4; c++) {
      HAL_GPIO_WritePin(GPIOF, COL1_Pin | COL2_Pin | COL3_Pin | COL4_Pin,
                        GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOF, COL_PINS[c], GPIO_PIN_RESET);
      vTaskDelay(pdMS_TO_TICKS(1));

      for (int r = 0; r < 4; r++) {
        if (HAL_GPIO_ReadPin(GPIOF, ROW_PINS[r]) == GPIO_PIN_RESET) {
          vTaskDelay(pdMS_TO_TICKS(5));
          if (HAL_GPIO_ReadPin(GPIOF, ROW_PINS[r]) == GPIO_PIN_RESET) {
            key = key_matrix_values[r][c];
            isKeyPressed = 1;
            while (HAL_GPIO_ReadPin(GPIOF, ROW_PINS[r]) == GPIO_PIN_RESET) {
              vTaskDelay(pdMS_TO_TICKS(10));
            }
            break;
          }
        }
      }
      if (isKeyPressed) break;
    }

    HAL_GPIO_WritePin(GPIOF, COL1_Pin | COL2_Pin | COL3_Pin | COL4_Pin,
                      GPIO_PIN_RESET);

    if (isKeyPressed == 1) {
      int len = strlen(key_buffer);

      xQueuePeek(queueRelayOUT, &isDoorOpen, 0);
      xQueueReceive(queueRegistrationControl, &isRegistration, 0);

      if (isRegistration == true) {
        if (key == '*') {
          isRegistration = false;
        }
        continue;
      }

      switch (key) {
        case '*':  // 清除輸入
          key_buffer[0] = '\0';
          break;
        case '#':  // 確認輸入
          printf("\r\n[Keypad] PIN Entered: %s\r\n", key_buffer);
          if (isUpdation == true) {
            SendPINUpdatedEvent(key_buffer);
            isUpdation = false;
          } else {
            SendPINEvent(key_buffer);
          }
          key_buffer[0] = '\0';
          break;
        case 'A':  // 更新密碼
          if (isDoorOpen == true) {
            snprintf(displayText.line1, sizeof(displayText.line1), "%s",
                     "PIN Update");
            isUpdation = true;
            key_buffer[0] = '\0';
          }
          break;
        case 'B':  // 註冊人臉
          if (isDoorOpen == true) {
            SendFaceRegistrationEvent();
            snprintf(displayText.line1, sizeof(displayText.line1), "%s",
                     "Face Registration");
            isRegistration = true;
            key_buffer[0] = '\0';
          }
          break;
        case 'C':  // 註冊卡片
          if (isDoorOpen == true) {
            xQueueSend(queueRFIDRegistration, &(bool){true}, 0);
            snprintf(displayText.line1, sizeof(displayText.line1), "%s",
                     "RFID Registration");
            isRegistration = true;
            key_buffer[0] = '\0';
          }
          break;
        case 'D':  // 回退輸入
          if (len > 0) {
            key_buffer[len - 1] = '\0';
          }
          break;
        default:  // 一般輸入
          if (len < 16) {
            key_buffer[len] = key;
            key_buffer[len + 1] = '\0';
          }
          break;
      }

      if (!isRegistration && !isUpdation) {
        snprintf(displayText.line1, sizeof(displayText.line1), "%s",
                 "Keypad Input");
      }
      snprintf(displayText.line2, sizeof(displayText.line2), "%s", key_buffer);
      xQueueSend(queueLCD, &displayText, portMAX_DELAY);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void taskRFID(void* pvParm) {
  LcdDisplay_t displayText;
  MFRC522_Init();
  uchar status;
  uchar uidBytes[MAX_LEN];
  char rfid[9] = {0};
  bool isRegistration = false;

  while (1) {
    xQueueReceive(queueRFIDRegistration, &isRegistration, 0);
    status = MFRC522_Request(PICC_REQIDL, uidBytes);
    if (status == MI_OK) {
      status = MFRC522_Anticoll(uidBytes);
      if (status == MI_OK) {
        if (isRegistration) {
          SendRFIDRegistrationEvent(uidBytes, 4);
          isRegistration = false;
        } else {
          SendRfidEvent(uidBytes, 4);
        }
        snprintf(rfid, sizeof(rfid), "%02x%02x%02x%02x", uidBytes[0],
                 uidBytes[1], uidBytes[2], uidBytes[3]);
        snprintf(displayText.line1, sizeof(displayText.line1), "%s",
                 "RFID Card Scanned");
        snprintf(displayText.line2, sizeof(displayText.line2), "%s", rfid);
        xQueueSend(queueLCD, &displayText, portMAX_DELAY);
        printf("\r\n[RFID] Card Scanned UID: %s\r\n", rfid);
      }
      MFRC522_Halt();
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

void taskRelayIN(void* pvParm) {
  bool open = false;
  while (1) {
    if (xQueueReceive(queueRelayIN, &open, portMAX_DELAY) == pdTRUE) {
      if (open) {
        HAL_GPIO_WritePin(GPIOD, RELAY_IN_Pin, GPIO_PIN_SET);
      } else {
        HAL_GPIO_WritePin(GPIOD, RELAY_IN_Pin, GPIO_PIN_RESET);
      }
    }
  }
}

void taskLed(void* pvParm) {
  TickType_t xTicksToWait = pdMS_TO_TICKS(1000);
  bool blinkState = false;
  char ledCommand;

  HAL_GPIO_WritePin(GPIOD, LED_B_Pin | LED_G_Pin | LED_R_Pin, GPIO_PIN_SET);

  while (1) {
    if (xQueueReceive(queueLed, &ledCommand, xTicksToWait) == pdTRUE) {
      HAL_GPIO_WritePin(GPIOD, LED_B_Pin | LED_G_Pin | LED_R_Pin, GPIO_PIN_SET);
      switch (ledCommand) {
        case 'R':
          HAL_GPIO_WritePin(GPIOD, LED_R_Pin, GPIO_PIN_RESET);
          break;
        case 'G':
          HAL_GPIO_WritePin(GPIOD, LED_G_Pin, GPIO_PIN_RESET);
          break;
        case 'B':
          HAL_GPIO_WritePin(GPIOD, LED_B_Pin, GPIO_PIN_RESET);
          break;
        case 'Y':
          HAL_GPIO_WritePin(GPIOD, LED_R_Pin, GPIO_PIN_RESET);
          HAL_GPIO_WritePin(GPIOD, LED_G_Pin, GPIO_PIN_RESET);
          break;
        default:
          break;
      }

      xTicksToWait = pdMS_TO_TICKS(5000);
    } else {
      if (blinkState) {
        HAL_GPIO_WritePin(GPIOD, LED_B_Pin | LED_G_Pin | LED_R_Pin,
                          GPIO_PIN_SET);
      } else {
        HAL_GPIO_WritePin(GPIOD, LED_B_Pin | LED_G_Pin | LED_R_Pin,
                          GPIO_PIN_RESET);
      }
      blinkState = !blinkState;

      xTicksToWait = pdMS_TO_TICKS(1000);
    }
  }
}

void taskLCD(void* pvParm) {
  const uint8_t lcd_addr = (0x27 << 1);
  LcdDisplay_t displayText;
  char cleanLine1[17];
  char cleanLine2[17];

  I2C_Scan();
  LCD_Init(lcd_addr);

  while (1) {
    if (xQueueReceive(queueLCD, &displayText, portMAX_DELAY) == pdTRUE) {
      snprintf(cleanLine1, sizeof(cleanLine1), "%-16.16s", displayText.line1);
      snprintf(cleanLine2, sizeof(cleanLine2), "%-16.16s", displayText.line2);

      LCD_SendCommand(lcd_addr, 0b10000000);
      LCD_SendString(lcd_addr, cleanLine1);
      LCD_SendCommand(lcd_addr, 0b11000000);
      LCD_SendString(lcd_addr, cleanLine2);
    }
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  switch (GPIO_Pin) {
    case SW1_Pin:  // 模擬關門
      if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_SET) {
        xQueueSendFromISR(queueRelayIN, &(bool){false},
                          &xHigherPriorityTaskWoken);
      }
      break;
    case SW2_Pin:  // 系統重設
      if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_SET) {
        SendSmartLockSignalFromISR(SIGNAL_SYSTEM_RESET,
                                   &xHigherPriorityTaskWoken);
      }
      break;
    case RELAY_OUT_Pin:  // 門狀態
      if (HAL_GPIO_ReadPin(RELAY_OUT_GPIO_Port, RELAY_OUT_Pin) ==
          GPIO_PIN_SET) {
        xQueueOverwriteFromISR(queueRelayOUT, &(bool){false},
                               &xHigherPriorityTaskWoken);
      } else {
        xQueueOverwriteFromISR(queueRelayOUT, &(bool){true},
                               &xHigherPriorityTaskWoken);
      }
      break;
    case SR505_OUT_Pin:  // 紅外線
      SendSmartLockSignalFromISR(SIGNAL_IR_TRIGGERED,
                                 &xHigherPriorityTaskWoken);
      break;
    default:
      break;
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART3_UART_Init();
  MX_SPI1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  queueKeypad = xQueueCreate(3, sizeof(char[17]));
  queueLed = xQueueCreate(3, sizeof(char));
  queueLCD = xQueueCreate(5, sizeof(LcdDisplay_t));
  queueRelayOUT = xQueueCreate(1, sizeof(bool));
  queueRelayIN = xQueueCreate(3, sizeof(bool));
  queueRFID = xQueueCreate(3, sizeof(char));
  queueRFIDRegistration = xQueueCreate(3, sizeof(bool));
  queueRegistrationControl = xQueueCreate(3, sizeof(bool));

  xTaskCreate(taskKeypad, "Keypad", 256, NULL, 3, &handleKeypad);
  xTaskCreate(taskRFID, "RFID", 256, NULL, 3, &handleRFID);
  xTaskCreate(taskLed, "LED", 128, NULL, 1, &handleLed);
  xTaskCreate(taskLCD, "LCD", 256, NULL, 1, &handleLCD);
  xTaskCreate(taskRelayIN, "RelayIN", 256, NULL, 2, &handleRelayIN);

  xTaskCreate((TaskFunction_t)StartTask_Tx, "TxTask", 512, NULL, 3, &handleTx);
  xTaskCreate((TaskFunction_t)StartTask_Rx, "RxTask", 512, NULL, 3, &handleRx);

  InitSmartLockComms();
  vTaskStartScheduler();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize(); /* Call init function for freertos objects (in
                           cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state
   */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n",
     file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
