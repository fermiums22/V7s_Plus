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
#define BASE_IR_RIGHT_Pin GPIO_PIN_6
#define BASE_IR_RIGHT_GPIO_Port GPIOE
#define BASE_IR_RIGHT_EXTI_IRQn EXTI4_15_IRQn
#define SIDE_IR_TRX_SIGNAL_Pin GPIO_PIN_0
#define SIDE_IR_TRX_SIGNAL_GPIO_Port GPIOC
#define CLIFF_LEFT_SIGNAL_Pin GPIO_PIN_1
#define CLIFF_LEFT_SIGNAL_GPIO_Port GPIOC
#define CLIFF_FRONT_L_SIGNAL_Pin GPIO_PIN_2
#define CLIFF_FRONT_L_SIGNAL_GPIO_Port GPIOC
#define FRONT_IR_PANEL_R_SIGNAL_Pin GPIO_PIN_3
#define FRONT_IR_PANEL_R_SIGNAL_GPIO_Port GPIOC
#define ROBOT_TOUCH_BUTTON_Pin GPIO_PIN_0
#define ROBOT_TOUCH_BUTTON_GPIO_Port GPIOA
#define ROBOT_TOUCH_BUTTON_EXTI_IRQn EXTI0_1_IRQn
#define VIN_24V_SENSE_Pin GPIO_PIN_1
#define VIN_24V_SENSE_GPIO_Port GPIOA
#define BATTERY_VOLTAGE_SENSE_Pin GPIO_PIN_2
#define BATTERY_VOLTAGE_SENSE_GPIO_Port GPIOA
#define CLIFF_RIGHT_SIGNAL_Pin GPIO_PIN_3
#define CLIFF_RIGHT_SIGNAL_GPIO_Port GPIOA
#define FRONT_IR_PANEL_F_SIGNAL_Pin GPIO_PIN_5
#define FRONT_IR_PANEL_F_SIGNAL_GPIO_Port GPIOA
#define BATTERY_CURRENT_SENSE_Pin GPIO_PIN_7
#define BATTERY_CURRENT_SENSE_GPIO_Port GPIOA
#define CLIFF_FRONT_R_SIGNAL_Pin GPIO_PIN_4
#define CLIFF_FRONT_R_SIGNAL_GPIO_Port GPIOC
#define MOTOR_R_VPROPI_Pin GPIO_PIN_5
#define MOTOR_R_VPROPI_GPIO_Port GPIOC
#define FRONT_IR_PANEL_L_SIGNAL_Pin GPIO_PIN_0
#define FRONT_IR_PANEL_L_SIGNAL_GPIO_Port GPIOB
#define MOTOR_L_VPROPI_Pin GPIO_PIN_1
#define MOTOR_L_VPROPI_GPIO_Port GPIOB
#define FRONT_IR_SENSOR_3V3_EN_Pin GPIO_PIN_7
#define FRONT_IR_SENSOR_3V3_EN_GPIO_Port GPIOE
#define MOTOR_R_ENC_SIGNAL_Pin GPIO_PIN_8
#define MOTOR_R_ENC_SIGNAL_GPIO_Port GPIOE
#define MOTOR_R_ENC_SIGNAL_EXTI_IRQn EXTI4_15_IRQn
#define BUMPER_HIT_RIGHT_Pin GPIO_PIN_12
#define BUMPER_HIT_RIGHT_GPIO_Port GPIOE
#define BUMPER_HIT_RIGHT_EXTI_IRQn EXTI4_15_IRQn
#define MOTOR_R_PHASE_Pin GPIO_PIN_13
#define MOTOR_R_PHASE_GPIO_Port GPIOE
#define BASE_IR_FRONT_R_Pin GPIO_PIN_15
#define BASE_IR_FRONT_R_GPIO_Port GPIOE
#define BASE_IR_FRONT_R_EXTI_IRQn EXTI4_15_IRQn
#define FRONT_IR_Q11_PULSE_Pin GPIO_PIN_10
#define FRONT_IR_Q11_PULSE_GPIO_Port GPIOB
#define BASE_IR_FRONT_L_Pin GPIO_PIN_11
#define BASE_IR_FRONT_L_GPIO_Port GPIOB
#define BASE_IR_FRONT_L_EXTI_IRQn EXTI4_15_IRQn
#define BUTTON_LED_YELLOW_PWM_Pin GPIO_PIN_15
#define BUTTON_LED_YELLOW_PWM_GPIO_Port GPIOB
#define MOTOR_L_NFAULT_Pin GPIO_PIN_8
#define MOTOR_L_NFAULT_GPIO_Port GPIOD
#define BASE_IR_LEFT_Pin GPIO_PIN_13
#define BASE_IR_LEFT_GPIO_Port GPIOD
#define BASE_IR_LEFT_EXTI_IRQn EXTI4_15_IRQn
#define MOTOR_R_NFAULT_Pin GPIO_PIN_14
#define MOTOR_R_NFAULT_GPIO_Port GPIOD
#define MOTOR_R_ENABLE_PWM_Pin GPIO_PIN_6
#define MOTOR_R_ENABLE_PWM_GPIO_Port GPIOC
#define CHARGER_ENABLE_PWM_Pin GPIO_PIN_7
#define CHARGER_ENABLE_PWM_GPIO_Port GPIOC
#define MOTOR_L_ENABLE_PWM_Pin GPIO_PIN_8
#define MOTOR_L_ENABLE_PWM_GPIO_Port GPIOC
#define Q24_Pin GPIO_PIN_8
#define Q24_GPIO_Port GPIOA
#define JP1_TX_Pin GPIO_PIN_9
#define JP1_TX_GPIO_Port GPIOA
#define JP1_RX_Pin GPIO_PIN_10
#define JP1_RX_GPIO_Port GPIOA
#define BUZZER1_Q17_Pin GPIO_PIN_11
#define BUZZER1_Q17_GPIO_Port GPIOA
#define CASTER_ODO_Pin GPIO_PIN_2
#define CASTER_ODO_GPIO_Port GPIOD
#define CASTER_ODO_EXTI_IRQn EXTI2_3_IRQn
#define MOTOR_L_ENC_SIGNAL_Pin GPIO_PIN_3
#define MOTOR_L_ENC_SIGNAL_GPIO_Port GPIOD
#define MOTOR_L_ENC_SIGNAL_EXTI_IRQn EXTI2_3_IRQn
#define BASE_IR_REAR_Pin GPIO_PIN_4
#define BASE_IR_REAR_GPIO_Port GPIOD
#define BASE_IR_REAR_EXTI_IRQn EXTI4_15_IRQn
#define BUMPER_HIT_LEFT_Pin GPIO_PIN_5
#define BUMPER_HIT_LEFT_GPIO_Port GPIOB
#define BUMPER_HIT_LEFT_EXTI_IRQn EXTI4_15_IRQn
#define MOTOR_L_PHASE_Pin GPIO_PIN_7
#define MOTOR_L_PHASE_GPIO_Port GPIOB
#define BUTTON_LED_ORANGE_PWM_Pin GPIO_PIN_9
#define BUTTON_LED_ORANGE_PWM_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
