/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SW1_Pin GPIO_PIN_3
#define SW1_GPIO_Port GPIOE
#define SW1_EXTI_IRQn EXTI3_IRQn
#define SW2_Pin GPIO_PIN_4
#define SW2_GPIO_Port GPIOE
#define SW2_EXTI_IRQn EXTI4_IRQn
#define SW3_Pin GPIO_PIN_5
#define SW3_GPIO_Port GPIOE
#define SW3_EXTI_IRQn EXTI9_5_IRQn
#define SW4_Pin GPIO_PIN_6
#define SW4_GPIO_Port GPIOE
#define SW4_EXTI_IRQn EXTI9_5_IRQn
#define COL1_Pin GPIO_PIN_2
#define COL1_GPIO_Port GPIOF
#define COL2_Pin GPIO_PIN_3
#define COL2_GPIO_Port GPIOF
#define COL3_Pin GPIO_PIN_4
#define COL3_GPIO_Port GPIOF
#define COL4_Pin GPIO_PIN_5
#define COL4_GPIO_Port GPIOF
#define ROW1_Pin GPIO_PIN_7
#define ROW1_GPIO_Port GPIOF
#define ROW2_Pin GPIO_PIN_8
#define ROW2_GPIO_Port GPIOF
#define ROW3_Pin GPIO_PIN_9
#define ROW3_GPIO_Port GPIOF
#define ROW4_Pin GPIO_PIN_10
#define ROW4_GPIO_Port GPIOF
#define SR505_OUT_Pin GPIO_PIN_0
#define SR505_OUT_GPIO_Port GPIOC
#define SR505_OUT_EXTI_IRQn EXTI0_IRQn
#define RC522_RST_Pin GPIO_PIN_4
#define RC522_RST_GPIO_Port GPIOA
#define RC522_IRQ_Pin GPIO_PIN_1
#define RC522_IRQ_GPIO_Port GPIOB
#define RC522_IRQ_EXTI_IRQn EXTI1_IRQn
#define LED_G_Pin GPIO_PIN_11
#define LED_G_GPIO_Port GPIOD
#define LED_B_Pin GPIO_PIN_12
#define LED_B_GPIO_Port GPIOD
#define LED_R_Pin GPIO_PIN_13
#define LED_R_GPIO_Port GPIOD
#define RELAY_IN_Pin GPIO_PIN_14
#define RELAY_IN_GPIO_Port GPIOD
#define RELAY_OUT_Pin GPIO_PIN_2
#define RELAY_OUT_GPIO_Port GPIOD
#define RELAY_OUT_EXTI_IRQn EXTI2_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
