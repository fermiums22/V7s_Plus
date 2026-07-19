/**
  ******************************************************************************
  * @file    charge_control.h
 * @brief   Closed-loop control for the active-high PC7 charger-enable PWM.
  *
 * PC7 is CubeMX-owned TIM3_CH2. The PWM channel is configured active-high:
 * CCR2 = 0 holds PC7 low (charger disabled); a non-zero compare gives an
 * active-high enable duty. No direct PC7 GPIO writes are permitted here.
  ******************************************************************************
  */
#ifndef CHARGE_CONTROL_H
#define CHARGE_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef enum
{
  CHARGE_CONTROL_DISABLED = 0,
  CHARGE_CONTROL_WAIT_DOCK,
  CHARGE_CONTROL_SOFT_START,
  CHARGE_CONTROL_CONSTANT_CURRENT,
  CHARGE_CONTROL_CONSTANT_VOLTAGE,
  CHARGE_CONTROL_DONE,
  CHARGE_CONTROL_FAULT
} ChargeControlState;

typedef enum
{
  CHARGE_FAULT_NONE        = 0,
  CHARGE_FAULT_DOCK_LOST   = 1,
  CHARGE_FAULT_VIN_RANGE   = 2,
  CHARGE_FAULT_OVERVOLTAGE = 3,
  CHARGE_FAULT_OVERCURRENT = 4,
  CHARGE_FAULT_SENSOR      = 5
} ChargeFault;

/* The PWM is initialized disabled. Charging can only start via an explicit
 * caller request (Modbus coil 7 in the application protocol). */
void ChargeControl_Init(TIM_HandleTypeDef *pwm_timer);
void ChargeControl_Start(void);
void ChargeControl_Stop(void);
void ChargeControl_SetAutoEnabled(uint8_t enabled);
void ChargeControl_Task(void);

uint8_t ChargeControl_Requested(void);
uint8_t ChargeControl_AutoEnabled(void);
uint8_t ChargeControl_OutputActive(void);
uint8_t ChargeControl_DockPresent(void);
uint16_t ChargeControl_Status(void);
uint16_t ChargeControl_Sequence(void);
ChargeControlState ChargeControl_State(void);
ChargeFault ChargeControl_Fault(void);
uint16_t ChargeControl_DutyPermille(void);
uint16_t ChargeControl_VbatMilliVolts(void);
int16_t ChargeControl_BatteryCurrentMilliAmps(void);
uint16_t ChargeControl_VinMilliVolts(void);

#ifdef __cplusplus
}
#endif

#endif /* CHARGE_CONTROL_H */
