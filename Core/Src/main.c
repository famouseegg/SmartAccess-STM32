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
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "queue.h"
#include "stdio.h"
#include "task.h"
#include "semphr.h"
#include "RC522.h"
#include "i2c_lcd.h"
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
TaskHandle_t handlePIR;
TaskHandle_t handleRFID;
TaskHandle_t handleRelay;
TaskHandle_t handleLed;
TaskHandle_t handleLCD;

QueueHandle_t queueKeypad;
QueueHandle_t queueRFID;
QueueHandle_t queueLed;
QueueHandle_t queueLCD;

SemaphoreHandle_t xPIR_Semaphore;
SemaphoreHandle_t xRelay_Semaphore;

I2C_LCD_HandleTypeDef lcd1;
I2C_LCD_HandleTypeDef lcd2;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void taskKeypad(void* pvParm){
    printf("Keypad Task Started\r\n");
    
    uint16_t COL_PINS[4] = {COL1_Pin, COL2_Pin, COL3_Pin, COL4_Pin};
    uint16_t ROW_PINS[4] = {ROW1_Pin, ROW2_Pin, ROW3_Pin, ROW4_Pin};
    
    
    char key_matrix_values[4][4] = {
        {'1',  '2',  '3',  'A'},
        {'4',  '5',  '6',  'B'},
        {'7',  '8',  '9', 'C'},
        {'*', '0', '#', 'D'}
    };

    
    HAL_GPIO_WritePin(GPIOF, COL1_Pin | COL2_Pin | COL3_Pin | COL4_Pin, GPIO_PIN_SET);

    while(1)
    {
        char mode = '0';
        uint8_t isKeyPressed = 0;

        
        for (int c = 0; c < 4; c++)
        {
            HAL_GPIO_WritePin(GPIOF, COL1_Pin | COL2_Pin | COL3_Pin | COL4_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOF, COL_PINS[c], GPIO_PIN_RESET);

            vTaskDelay(pdMS_TO_TICKS(1)); 

            for (int r = 0; r < 4; r++)
            {
                if (HAL_GPIO_ReadPin(GPIOF, ROW_PINS[r]) == GPIO_PIN_RESET)
                {
                    
                    vTaskDelay(pdMS_TO_TICKS(5));
                    if (HAL_GPIO_ReadPin(GPIOF, ROW_PINS[r]) == GPIO_PIN_RESET)
                    {
                        mode = key_matrix_values[r][c];
                        printf("Key Pressed: %c\r\n", mode);
                        isKeyPressed = 1;
                        
                      
                        while(HAL_GPIO_ReadPin(GPIOF, ROW_PINS[r]) == GPIO_PIN_RESET)
                        {
                            vTaskDelay(pdMS_TO_TICKS(10)); 
                        }
                        break; 
                    }
                }
            }
            if (isKeyPressed) break;
        }

        HAL_GPIO_WritePin(GPIOF, COL1_Pin | COL2_Pin | COL3_Pin | COL4_Pin, GPIO_PIN_RESET);

        if (isKeyPressed == 1) 
        {
            // xQueueSend(queueSwitch, &mode, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void taskPIR(void* pvParm)
{
    printf("SR505 PIR Task Started\r\n");
  
    while(1)
    {
        
        if (xSemaphoreTake(xPIR_Semaphore, portMAX_DELAY) == pdTRUE)
        {
            printf("Motion Detected\r\n");
            xSemaphoreTake(xPIR_Semaphore, 0);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        
        
    }
}

void taskRFID(void* pvParm)
{
  printf("RFID Task Started, Waiting for Card...\r\n");
  
  MFRC522_Init(); 

  uchar status;
  uchar str[MAX_LEN];

  while(1)
  {
      
    status = MFRC522_Request(PICC_REQIDL, str);
    if (status == MI_OK)
    {
      printf("\r\n[RFID] Card Detected!\r\n");
      printf("[RFID] Type Code: 0x%02X%02X\r\n", str[0], str[1]);
      
      status = MFRC522_Anticoll(str);
      if (status == MI_OK)
      {
          printf("[RFID] UID: %02X:%02X:%02X:%02X\r\n", str[0], str[1], str[2], str[3]);
      }
      
      //卡片睡眠，避免讀同一張卡
      MFRC522_Halt(); 
      //避免連續觸發
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else
    {
        // 沒有卡片，繼續等待
        vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}
void taskRelay(void* pvParm)
{
    while(1)
    {
        if (xSemaphoreTake(xRelay_Semaphore, portMAX_DELAY) == pdTRUE)
        {
            printf("Relay Activated\r\n");
            HAL_GPIO_WritePin(GPIOD, RELAY_Pin, GPIO_PIN_SET);
            vTaskDelay(pdMS_TO_TICKS(5000));
            HAL_GPIO_WritePin(GPIOD, RELAY_Pin, GPIO_PIN_RESET);
            printf("Relay Deactivated\r\n");
        }
    }
}
void taskLed(void* pvParm)
{

  while(1)
  {
     HAL_GPIO_WritePin(GPIOD, LED_R_Pin, GPIO_PIN_RESET);
     HAL_GPIO_WritePin(GPIOD, LED_G_Pin, GPIO_PIN_RESET);
     HAL_GPIO_WritePin(GPIOD, LED_B_Pin, GPIO_PIN_SET);
    //  HAL_GPIO_WritePin(GPIOD, LED_R_Pin, GPIO_PIN_RESET);
      // char ledCommand;
      // if (xQueueReceive(queueLed, &ledCommand, portMAX_DELAY) == pdTRUE)
      // {
          // Switch (ledCommand) {
          //     case 'R':
          //         HAL_GPIO_WritePin(GPIOD, LED_R, GPIO_PIN_SET);
        
          //         break;
          //     case 'G':
          //         HAL_GPIO_WritePin(GPIOD, LED_G, GPIO_PIN_SET);
        
          //         break;
          //     case 'B':
          //         HAL_GPIO_WritePin(GPIOD, LED_B, GPIO_PIN_SET);
          //         break;           
          //     default:
                  
          // }
      // }
      vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void init_lcds(void) {
    lcd1.hi2c = &hi2c1;     // hi2c1 is your I2C handler
    lcd1.address = 0x4E;    // I2C address for the first LCD
    lcd_init(&lcd1);        // Initialize the first LCD

    lcd2.hi2c = &hi2c1;     // Use the same or different I2C handler
    lcd2.address = 0x7E;    // I2C address for the second LCD
    lcd_init(&lcd2);        // Initialize the second LCD
}
void taskLCD(void* pvParm)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    // 💡 在任務開頭進行 LCD 初始化，確保核心跑起來後才動硬體
    lcd1.hi2c = &hi2c1;
    lcd1.address = 0x4E; // 根據你 LCD 實際地址調整
    lcd_init(&lcd1);
    lcd_clear(&lcd1);
    lcd_puts(&lcd1, "LCD 1 Ready");

    printf("LCD Task Started\r\n");

    while(1)
    {
        // 不要每一秒都瘋狂覆寫一模一樣的字，LCD 畫面會閃爍且霸佔 I2C
        // 這裡做一次簡單的顯示測試
        lcd_clear(&lcd1);
        lcd_gotoxy(&lcd1, 0, 0);
        lcd_puts(&lcd1, "System Running");
        lcd_gotoxy(&lcd1, 0, 1);
        lcd_puts(&lcd1, "LCD 1 Displaying");
        // 讓出 CPU 執行權 5 秒
        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == SR505_OUT_Pin) 
  {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      
      xSemaphoreGiveFromISR(xPIR_Semaphore, &xHigherPriorityTaskWoken);
      
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
  
  // 這裡的邏輯是：當 SW1 或 SW2 被按下時，讀取按鈕狀態並將對應的模式發送到 queueSwitch
  uint8_t mode;
  uint8_t isKeyPressed = 0;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  switch (GPIO_Pin) {
    case SW1_Pin: {  // sw1 PE3
      if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == 1) {
        mode = 0;
        isKeyPressed = 1;
      }
      break;
    }
    case SW2_Pin: {  // sw2 PE4
      if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == 1) {
        mode = 1;
        isKeyPressed = 1;
      }
      break;
    }
    case SW3_Pin: {  // sw3 PE5
      if (HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) == 0) {
        mode = 2;
        isKeyPressed = 1;
      }
      break;
    }
    case SW4_Pin: {  // sw4 PE6
      if (HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin) == 0) {
        mode = 3;
        isKeyPressed = 1;
      }
      break;
    }
    default:{
      break;
    }
      
  }

  if (isKeyPressed == 1) {
    printf("Button Pressed: Mode %d\r\n", mode);
  }
  

  // if (isKeyPressed == 1) {
  //   xQueueSendFromISR(queueSwitch, &mode, &xHigherPriorityTaskWoken);
  //   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  // }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
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

  queueKeypad = xQueueCreate(3, sizeof(char));
  queueLed = xQueueCreate(3, sizeof(char));
  queueRFID = xQueueCreate(10, sizeof(char));
  queueLCD = xQueueCreate(10, sizeof(char));

  xPIR_Semaphore = xSemaphoreCreateBinary();
  xRelay_Semaphore = xSemaphoreCreateBinary();
  
  xTaskCreate(taskPIR,   "PIR",   256, NULL, 4, &handlePIR);   
  xTaskCreate(taskRFID,  "RFID",  256, NULL, 3, &handleRFID);  
  xTaskCreate(taskKeypad,"Keypad",256, NULL, 2, &handleKeypad);
  xTaskCreate(taskRelay, "Relay", 128, NULL, 2, &handleRelay); 
  xTaskCreate(taskLed,   "LED",   128, NULL, 1, &handleLed);   
  xTaskCreate(taskLCD,   "LCD",   256, NULL, 1, &handleLCD);   

  vTaskStartScheduler();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in freertos.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
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
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
