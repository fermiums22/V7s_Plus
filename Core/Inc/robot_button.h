/**
  ******************************************************************************
  * @file    robot_button.h
  * @brief   J12 touch button and two hardware-PWM backlight channels.
  ******************************************************************************
  */
#ifndef ROBOT_BUTTON_H
#define ROBOT_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

void RobotButton_Init(TIM_HandleTypeDef *yellow_pwm, TIM_HandleTypeDef *orange_pwm);
void RobotButton_OnEdge(void);
void RobotButton_Task(void);

uint8_t RobotButton_RawLevel(void);
uint8_t RobotButton_Pressed(void);
uint16_t RobotButton_YellowPermille(void);
uint16_t RobotButton_OrangePermille(void);

void RobotButton_SetEnabledIndicator(uint8_t enabled);
void RobotButton_SetYellowPermille(uint16_t permille);
void RobotButton_SetOrangePermille(uint16_t permille);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_BUTTON_H */
