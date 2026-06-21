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
#include "stm32f0xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SWITCHED_SENSOR_5V_EN_Pin GPIO_PIN_5
#define SWITCHED_SENSOR_5V_EN_GPIO_Port GPIOE
#define MOTOR_R_VPROPI_Pin GPIO_PIN_5
#define MOTOR_R_VPROPI_GPIO_Port GPIOC
#define MOTOR_L_VPROPI_Pin GPIO_PIN_1
#define MOTOR_L_VPROPI_GPIO_Port GPIOB
#define FRONT_IR_SENSOR_3V3_EN_Pin GPIO_PIN_7
#define FRONT_IR_SENSOR_3V3_EN_GPIO_Port GPIOE
#define MOTOR_R_ENC_SIGNAL_Pin GPIO_PIN_8
#define MOTOR_R_ENC_SIGNAL_GPIO_Port GPIOE
#define MOTOR_R_PHASE_Pin GPIO_PIN_13
#define MOTOR_R_PHASE_GPIO_Port GPIOE
#define FRONT_IR_Q11_TEST_PULSE_Pin GPIO_PIN_10
#define FRONT_IR_Q11_TEST_PULSE_GPIO_Port GPIOB
#define MOTOR_L_NFAULT_Pin GPIO_PIN_8
#define MOTOR_L_NFAULT_GPIO_Port GPIOD
#define MOTOR_R_NFAULT_Pin GPIO_PIN_14
#define MOTOR_R_NFAULT_GPIO_Port GPIOD
#define MOTOR_R_ENABLE_PWM_Pin GPIO_PIN_6
#define MOTOR_R_ENABLE_PWM_GPIO_Port GPIOC
#define MOTOR_L_ENABLE_PWM_Pin GPIO_PIN_8
#define MOTOR_L_ENABLE_PWM_GPIO_Port GPIOC
#define Q24_Pin GPIO_PIN_8
#define Q24_GPIO_Port GPIOA
#define JP1_TX_Pin GPIO_PIN_9
#define JP1_TX_GPIO_Port GPIOA
#define JP1_RX_Pin GPIO_PIN_10
#define JP1_RX_GPIO_Port GPIOA
#define MOTOR_L_ENC_SIGNAL_Pin GPIO_PIN_3
#define MOTOR_L_ENC_SIGNAL_GPIO_Port GPIOD
#define MOTOR_L_PHASE_Pin GPIO_PIN_7
#define MOTOR_L_PHASE_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
