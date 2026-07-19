/**
  ******************************************************************************
  * @file    docking.h
  * @brief   Slow local IR-beacon docking controller.
  ******************************************************************************
  */
#ifndef DOCKING_H
#define DOCKING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  DOCKING_IDLE = 0,
  DOCKING_SEARCH,
  DOCKING_TURN_LEFT,
  DOCKING_TURN_RIGHT,
  DOCKING_APPROACH,
  DOCKING_DOCKED,
  DOCKING_STOPPED,
  DOCKING_FAILED_TIMEOUT,
  DOCKING_FAILED_BUMPER,
  DOCKING_FAILED_MOTOR
} DockingState;

void Docking_Init(void);
uint8_t Docking_Start(void);
void Docking_Stop(void);
void Docking_Task(void);

uint8_t Docking_Active(void);
DockingState Docking_State(void);
uint16_t Docking_ElapsedSeconds(void);
const char *Docking_StateName(DockingState state);

#ifdef __cplusplus
}
#endif

#endif /* DOCKING_H */
