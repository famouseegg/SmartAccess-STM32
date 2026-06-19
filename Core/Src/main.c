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
#include "pb_encode.h"
#include "message.pb.h"
#include "queue.h"
#include "pb_decode.h"

// 定義串列埠傳輸的最大緩衝區大小 (根據最長的 Protobuf 封包決定)
#define PROTO_BUFFER_SIZE 128
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
TaskHandle_t handleTx;
TaskHandle_t handleRx;
TaskHandle_t handleIsopen;

QueueHandle_t queueKeypad;
QueueHandle_t queueRFID;
QueueHandle_t queueLed;
QueueHandle_t queueLCD;
QueueHandle_t queueRelay;

SemaphoreHandle_t xPIR_Semaphore;


// FreeRTOS 佇列控制塊，用於將要傳送的訊息交給 Tx Task
osMessageQId smartLockTxQueueHandle;
extern UART_HandleTypeDef huart2; 

// 接收快取與旗標 (若使用 UART+DMA 接收)
uint8_t rx_buffer[PROTO_BUFFER_SIZE];
volatile uint16_t rx_len = 0;
volatile uint8_t rx_flag = 0;

typedef enum {
    SIGNAL_IR_TRIGGERED,       // 紅外線觸發
    SIGNAL_FACE_REGISTRATION,  // 人臉註冊
    SIGNAL_RFID_REGISTRATION,  // RFID 註冊模式
    SIGNAL_SYSTEM_RESET        // 系統重設
} SmartLockSignal_t;

typedef enum {
    RX_STATE_WAIT_HEADER1,
    RX_STATE_WAIT_HEADER2,
    RX_STATE_WAIT_LEN,
    RX_STATE_DATA
} RxState_t;

typedef struct{
  char line1[17];
  char line2[17];
}LcdDisplay_t;

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
void StartTask_Tx(void *argument)
{
    // 將編碼快取移到 Task 內部作為局部變數（或全域靜態）
    uint8_t encode_buffer[PROTO_BUFFER_SIZE];
    
    for(;;) {
        access_control_SmartLockPacket* p_packet = NULL;
        
        // 從 Queue 中取得封包結構體的「指標」
        osStatus_t status = osMessageQueueGet(smartLockTxQueueHandle, &p_packet, NULL, osWaitForever);
        
        // 確保指標不為空，且讀取成功
        if (status == osOK && p_packet != NULL) {
            
            // 建立 Nanopb 輸出流
            pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));
            
            // 修正：傳入解開指標後的實體結構體數據 (*p_packet)
            bool encode_status = pb_encode(&stream, access_control_SmartLockPacket_fields, p_packet);
            
            if (encode_status) {
                size_t bytes_written = stream.bytes_written;
                // 透過 UART 發送給樹莓派
                HAL_UART_Transmit(&huart2, encode_buffer, bytes_written, HAL_MAX_DELAY);
            }
            
            // ⚠️ 重要：因為 Send 函式使用的是全域靜態或動態記憶體，
            // 這裡不需要額外動作，但若是用動態配置則需在此釋放。
        }
    }
}

// 方便其他模組呼叫的 API：例如傳送密碼輸入事件
void SendPasswordEvent(const char* pwd) {
    static access_control_SmartLockPacket packet = access_control_SmartLockPacket_init_default;
    packet.sequence = 1; // 可以用全域變數累加
    packet.which_packet_body = access_control_SmartLockPacket_stm32_message_tag;
    
    packet.packet_body.stm32_message.which_body = access_control_Stm32Message_password_input_tag;
    strncpy(packet.packet_body.stm32_message.body.password_input.password, pwd, sizeof(packet.packet_body.stm32_message.body.password_input.password));
    
    // 寫入佇列 (不等待，若佇列滿了就放棄或回傳錯誤)
    access_control_SmartLockPacket* p_packet = &packet;
    osMessageQueuePut(smartLockTxQueueHandle, &p_packet, 0, 0);
}
void SendRfidEvent(const uint8_t* uid_data, uint16_t uid_len) {
    // 使用 static 確保函式結束後，這塊記憶體依然活著，直到 Tx Task 把他轉成二進位送出
    static access_control_SmartLockPacket packet;
    static uint32_t tx_sequence = 0;

    // 初始化封包結構體
    memset(&packet, 0, sizeof(packet));
    packet.sequence = ++tx_sequence;
    packet.which_packet_body = access_control_SmartLockPacket_stm32_message_tag;
    
    // 指定內部分支為 rfid_scanned
    packet.packet_body.stm32_message.which_body = access_control_Stm32Message_rfid_scanned_tag;
    
    // 處理 Nanopb 的 bytes 型態賦值：必須給予長度(size)與資料(bytes)
    // 限制最大長度防止超出 Nanopb 生成的陣列邊界 (一般 UID 最大為 10 位元組，安全起見限制一下)
    uint16_t max_bytes_size = sizeof(packet.packet_body.stm32_message.body.rfid_scanned.uid.bytes);
    if (uid_len > max_bytes_size) {
        uid_len = max_bytes_size;
    }
    
    packet.packet_body.stm32_message.body.rfid_scanned.uid.size = uid_len;
    memcpy(packet.packet_body.stm32_message.body.rfid_scanned.uid.bytes, uid_data, uid_len);
    
    // 寫入佇列，將指標傳送給 Tx Task 處理
    access_control_SmartLockPacket* p_packet = &packet;
    osMessageQueuePut(smartLockTxQueueHandle, &p_packet, 0, 0);
}
void SendSmartLockSignal(SmartLockSignal_t signal) {
    // 使用 static 確保記憶體在 Tx Task 序列化完成前一直有效
    static access_control_SmartLockPacket packet;
    static uint32_t tx_sequence = 0;

    // 初始化與清空封包
    memset(&packet, 0, sizeof(packet));
    packet.sequence = ++tx_sequence;
    packet.which_packet_body = access_control_SmartLockPacket_stm32_message_tag;
    
    // 核心邏輯：用 switch-case 根據傳入的 enum 決定 Nanopb 的 which_body 標籤
    switch (signal) {
        case SIGNAL_IR_TRIGGERED:
            packet.packet_body.stm32_message.which_body = access_control_Stm32Message_ir_triggered_tag;
            break;
            
        case SIGNAL_FACE_REGISTRATION:
            packet.packet_body.stm32_message.which_body = access_control_Stm32Message_face_registration_tag;
            break;
            
        case SIGNAL_RFID_REGISTRATION:
            packet.packet_body.stm32_message.which_body = access_control_Stm32Message_rfid_registration_tag;
            break;
            
        case SIGNAL_SYSTEM_RESET:
            packet.packet_body.stm32_message.which_body = access_control_Stm32Message_system_reset_tag;
            break;
            
        default:
            return; // 未知訊號直接結束，不發送
    }
    
    // 將打包好的結構體指標送進發送佇列
    access_control_SmartLockPacket* p_packet = &packet;
    osMessageQueuePut(smartLockTxQueueHandle, &p_packet, 0, 0);
}
void LcdDisplay(char line1[16], char line2[16]) {
    LcdDisplay_t displaytext;

    snprintf(displaytext.line1, sizeof(displaytext.line1), "%s", line1);
    snprintf(displaytext.line2, sizeof(displaytext.line2), "%s", line2);

    xQueueSend(queueLCD, &displaytext, portMAX_DELAY);
}
void taskKeypad(void* pvParm){
   printf("Keypad Task Started\r\n");
    uint16_t COL_PINS[4] = {COL1_Pin, COL2_Pin, COL3_Pin, COL4_Pin};
    uint16_t ROW_PINS[4] = {ROW1_Pin, ROW2_Pin, ROW3_Pin, ROW4_Pin};

    static char key_buffer[17] = {0}; // 用於存儲按鍵輸入的緩衝區，最大長度為16個字符 + 結束符
	
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
                        // printf("Key Pressed: %c\r\n", mode);
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
          int len = strlen(key_buffer);
          if(mode == '*'){
            if(len > 0){
              key_buffer[len-1] = '\0';
            }
          }
          else if(mode == '#'){
            SendPasswordEvent(key_buffer);
            key_buffer[0] = '\0';
					}
          else{
            if(len < 16){
              key_buffer[len] = mode;
              key_buffer[len+1] = '\0';
            }
          }
					LcdDisplay("keypad: ",key_buffer);
          printf("Current Key Buffer: %s\r\n", key_buffer);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void taskPIR(void* pvParm)
{
  printf("Running PIR Task\r\n");
    while(1)
    {
        
        if (xSemaphoreTake(xPIR_Semaphore, portMAX_DELAY) == pdTRUE)
        {
            SendSmartLockSignal(SIGNAL_IR_TRIGGERED);
            xSemaphoreTake(xPIR_Semaphore, 0);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        
    }
}

void taskRFID(void* pvParm)
{
  
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
					// 讓樹莓派判斷能不能開門
					SendRfidEvent(str, 4);
					LcdDisplay("RFID Scanned", "Verifying...");
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
void taskIsOpen(void* pvParm)
{
  uint8_t detectingDoorClosing = 0;
  uint8_t isOpen = 0;
  while(1)
  {
    if(HAL_GPIO_ReadPin(GPIOD, ISOPEN_Pin))
    {
      isOpen = 1;
      detectingDoorClosing = 1;
    }
    else
    {
      isOpen = 0;
      if(detectingDoorClosing)
      {
        xQueueSend(queueRelay, &isOpen, portMAX_DELAY);
        detectingDoorClosing = 0;
      }
      // printf("Door is %s\r\n", isOpen ? "Open" : "Closed");
     

    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void taskRelay(void* pvParm)
{
  uint8_t open = 0;
  while(1)
  {
    if(xQueueReceive(queueRelay,&open, portMAX_DELAY) == pdTRUE)
    {   
      if(open)
      {
        HAL_GPIO_WritePin(GPIOD, RELAY_Pin, GPIO_PIN_SET);      
      }
      else
      {
        HAL_GPIO_WritePin(GPIOD, RELAY_Pin, GPIO_PIN_RESET);
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}
void taskLed(void* pvParm)
{
  HAL_GPIO_WritePin(GPIOD, LED_B_Pin | LED_G_Pin | LED_R_Pin, GPIO_PIN_SET);
  while(1)
  {
    char ledCommand;
    if (xQueueReceive(queueLed, &ledCommand, portMAX_DELAY) == pdTRUE)
    {
      HAL_GPIO_WritePin(GPIOD, LED_B_Pin | LED_G_Pin | LED_R_Pin, GPIO_PIN_SET);
      switch (ledCommand) {
          case 'R':
              HAL_GPIO_WritePin(GPIOD, LED_R_Pin, GPIO_PIN_RESET);
              break;
          case 'G':
              HAL_GPIO_WritePin(GPIOD, LED_G_Pin, GPIO_PIN_RESET);
    
              break;
          case 'Y':
              HAL_GPIO_WritePin(GPIOD, LED_R_Pin, GPIO_PIN_RESET);
              HAL_GPIO_WritePin(GPIOD, LED_G_Pin, GPIO_PIN_RESET);
              break;           
          default:
              break;
      }
    }
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
    lcd1.hi2c = &hi2c1;
    lcd1.address = 0x4E; // 根據你 LCD 實際地址調整
    lcd_init(&lcd1);
    lcd_clear(&lcd1);
    lcd_puts(&lcd1, "System Ready");
    
		LcdDisplay_t displayText;// 用於接收從 queueLCD 發送的字串

    while(1)
    {   
        if(xQueueReceive(queueLCD,&displayText, portMAX_DELAY) == pdTRUE)
        {
          lcd_clear(&lcd1);
          lcd_gotoxy(&lcd1, 0, 0);
          lcd_puts(&lcd1, displayText.line1);
          
          // 第二行顯示完整的字串
          lcd_gotoxy(&lcd1, 0, 1);
          lcd_puts(&lcd1, displayText.line2); 
            
        }
        vTaskDelay(pdMS_TO_TICKS(10));
      }
}
// 初始化 Queue (在 main 中呼叫)
void InitSmartLockComms(void) {
  osMessageQueueId_t smartLockTxQueueHandle;

  // 請把這行放進你的 Queue 初始化函式中（例如 main 裡的 MX_FREERTOS_Init）
  smartLockTxQueueHandle = osMessageQueueNew(8, sizeof(access_control_SmartLockPacket*), NULL);
    
    // 啟動 UART 接收中斷 (建議使用 IDLE 中斷 + DMA 接收不定長度資料)
    // HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, PROTO_BUFFER_SIZE);
}

void StartTask_Rx(void *argument)
{
    access_control_SmartLockPacket rx_packet;
    
    for(;;) {
        // 這裡配合 UART 接收旗標，可以使用 FreeRTOS Task Notification 或 Semaphore 阻塞
        // 假設收到資料後，中斷會釋放訊號或將 rx_flag 設為 1
        if (rx_flag == 1) {
            
            // 建立 Nanopb 輸入流
            pb_istream_t stream = pb_istream_from_buffer(rx_buffer, rx_len);
            
            // 清空接收結構體
            memset(&rx_packet, 0, sizeof(rx_packet));
            
            // 開始解碼
            bool status = pb_decode(&stream, access_control_SmartLockPacket_fields, &rx_packet);
            
            if (status) {
                // 1. 檢查是否為給 PiMessage 包裹
                if (rx_packet.which_packet_body == access_control_SmartLockPacket_pi_message_tag) {
                    
                    access_control_PiMessage *pi_msg = &rx_packet.packet_body.pi_message;
                    
                    // 2. 根據 oneof body 判斷樹莓派傳來的是什麼指令
                    switch (pi_msg->which_body) {
                        
                        case access_control_PiMessage_unlock_tag:{
													
                            // 執行解鎖邏輯 
                            uint8_t open = 1;
                            xQueueSend(queueRelay, &open, portMAX_DELAY);
                            // 可以選擇回傳 CommandResponse 給樹莓派
                            break;
												} 
                        case access_control_PiMessage_control_rgb_led_tag:{
                            // 控制 RGB LED 燈號
														access_control_ControlRgbLedCommand *led_cmd = &pi_msg->body.control_rgb_led;
    
														// 2. 讀取紅、綠、藍的布林值 (true / false)
														bool r_status = led_cmd->red;
														bool g_status = led_cmd->green;
														bool b_status = led_cmd->blue;

														// 3. 根據讀到的狀態，決定要送什麼字元給你的 taskLed
														char led_color = '0'; // 預設關閉或無動作

														if (r_status && g_status) {
																led_color = 'Y';  // 紅 + 綠 = 黃燈 (對應你 taskLed 裡的 case 'Y')
														} else if (r_status) {
																led_color = 'R';  // 紅燈
														} else if (g_status) {
																led_color = 'G';  // 綠燈
														} else if (b_status) {
																led_color = 'B';  // 藍燈 (你可以在 taskLed 的 switch 補上 case 'B')
														}

														// 4. 將結果送入 queueLed
														if (led_color != '0') {
																xQueueSend(queueLed, &led_color, portMAX_DELAY);
														}

														// 可選：同時在 LCD 上顯示當前亮燈狀態
														// LcdDisplay("LED Control", "Updating Color");
														
														break;
												}
                            
                        case access_control_PiMessage_action_response_tag:
                            // 處理樹莓派對 STM32 之前請求的回應 (例如：人臉辨識結果)
                            if(pi_msg->body.action_response.status == access_control_StatusType_STATUS_TYPE_OK) {
                                // 驗證成功，點綠燈
                                LcdDisplay("Verification","successful");
																char led = 'G';
																xQueueSend(queueLed,&led,portMAX_DELAY);
                            } else {
                                // 驗證失敗處理
																LcdDisplay("Verification","failed");
																char led = 'R';
																xQueueSend(queueLed,&led,portMAX_DELAY);
                            }
                            break;
                            
                        default:
                            // 未知指令
                            break;
                    }
                }
            } else {
                // 解碼失敗處理
                // printf("Decoding failed: %s\n", PB_GET_ERROR(&stream));
            }
            
            // 處理完畢，重置快取，重新開啟 UART 接收
            rx_flag = 0;
            rx_len = 0;
            // HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buffer, PROTO_BUFFER_SIZE);
        }
        
        osDelay(10); // 小延遲防看門狗，若用 Semaphore 阻塞則不需此延遲
    }
}
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

  uint8_t mode=9;
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
    case SR505_OUT_Pin: {
      xSemaphoreGiveFromISR(xPIR_Semaphore, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
      break;
    }
    default:{
      break;
    }
      
  }

  if (isKeyPressed == 1) {
    switch(mode){
			case 0:
				//臉部註冊
				SendSmartLockSignal(SIGNAL_FACE_REGISTRATION);
				break;
			case 1:
				//RFID註冊
				SendSmartLockSignal(SIGNAL_RFID_REGISTRATION);
				break;
			case 2:
				//系統重設
				SendSmartLockSignal(SIGNAL_SYSTEM_RESET);
				break;
			case 3:
				break;
		}
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

  InitSmartLockComms(); // 初始化與樹莓派通訊的 Queue 和 UART 接收

  queueKeypad = xQueueCreate(3, sizeof(char[17]));
  queueLed = xQueueCreate(3, sizeof(char));
  queueRFID = xQueueCreate(10, sizeof(char));
  queueLCD = xQueueCreate(10, sizeof(LcdDisplay_t));
  queueRelay = xQueueCreate(3, sizeof(uint8_t));

  xPIR_Semaphore = xSemaphoreCreateBinary();
  
  xTaskCreate(taskIsOpen,"IsOpen",128,NULL,4,&handleIsopen);
  xTaskCreate(taskPIR,   "PIR",   256, NULL, 4, &handlePIR);   
  xTaskCreate(taskRFID,  "RFID",  256, NULL, 3, &handleRFID);  
  xTaskCreate(taskKeypad,"Keypad",256, NULL, 3, &handleKeypad);
  xTaskCreate(taskRelay, "Relay", 256, NULL, 2, &handleRelay); 
  xTaskCreate(taskLed,   "LED",   128, NULL, 1, &handleLed);   
  xTaskCreate(taskLCD,   "LCD",   256, NULL, 1, &handleLCD);   
  xTaskCreate(StartTask_Tx, "TxTask", 512, NULL, 1, &handleTx);
  xTaskCreate(StartTask_Rx, "RxTask", 512, NULL, 1, &handleRx);
  

  vTaskStartScheduler();
  /* USER CODE END 2 */

  /* Init scheduler */
  // osKernelInitialize();  /* Call init function for freertos objects (in freertos.c) */
  // MX_FREERTOS_Init();

  // /* Start scheduler */
  // osKernelStart();

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
