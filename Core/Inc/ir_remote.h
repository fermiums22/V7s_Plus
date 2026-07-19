/**
  ******************************************************************************
  * @file    ir_remote.h
  * @brief   Timestamp and dispatch dock IR receiver edges to the decoder.
  ******************************************************************************
  */
#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef enum
{
  IR_RX_FRONT_L = 0,
  IR_RX_FRONT_R,
  IR_RX_LEFT,
  IR_RX_RIGHT,
  IR_RX_REAR
} IrRxDir;

/* TIM6 is CubeMX-generated as a 1 MHz free-running edge timestamp. */
void IrRemote_Init(TIM_HandleTypeDef *us_timer);
/* EXTI dispatcher hook for all five dock receivers. */
void IrRemote_OnEdge(IrRxDir ch);

#ifdef __cplusplus
}
#endif

#endif /* IR_REMOTE_H */
