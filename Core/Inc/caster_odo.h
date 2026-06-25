/**
  ******************************************************************************
  * @file    caster_odo.h
  * @brief   Front caster-wheel odometry (J20) - EXTI edge counter on PD2.
  *
  *  The front caster has a black/white checkered disc read by a reflective IR
  *  sensor (J20). The receiver feeds U10 (LM393) channel 1, a Schmitt trigger,
  *  whose squared output (U10:1 / OUT1) lands on STM32 PD2. Each black<->white
  *  transition toggles PD2; we count BOTH edges via EXTI2 for finest resolution.
  *
  *  Power: the emitter (J20:4, via 3.9R) and U10 are on SWITCHED_SENSOR_5V, so
  *  PE5 (SWITCHED_SENSOR_5V_EN) must be high - FrontIrBumper_Init() does that.
  *  User-owned; EXTI configured here (CubeMX does not own PD2).
  ******************************************************************************
  */
#ifndef CASTER_ODO_H
#define CASTER_ODO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void     CasterOdo_Init(void);
void     CasterOdo_OnEdge(void);  /* EXTI dispatch hook (called from main.c) */
uint32_t CasterOdo_Count(void);   /* edge count since boot / last reset */
void     CasterOdo_Reset(void);
int      CasterOdo_Level(void);    /* instantaneous PD2 level (1/0) */

#ifdef __cplusplus
}
#endif

#endif /* CASTER_ODO_H */
