/**
  ******************************************************************************
  * @file    ir_remote.h
  * @brief   NEC IR remote/base decode on the 5 dock-beacon receivers, with the
  *          direction the command was seen from.
  *
  *  The same 5 demodulated IR receivers that base_ir.c polls for dock-homing
  *  also carry NEC command frames (38 kHz carrier already stripped by the
  *  receiver module: output idles HIGH, pulses LOW for each burst). Each receiver
  *  is decoded INDEPENDENTLY by its own NEC state machine fed from EXTI edge
  *  timestamps (a free-running 1 us timer gives the mark/space widths). Decoded
  *  frames that land in the same short time window with the same command are
  *  merged into one event whose dir_mask says which receivers saw it - that is
  *  the direction the press came from.
  *
  *  Receiver -> STM32 pin (same order/labels as base_ir):
  *     IR_RX_FRONT_L  PB11   IR_RX_FRONT_R  PE15   IR_RX_LEFT  PD13
  *     IR_RX_RIGHT    PE6    IR_RX_REAR     PD4
  *
  *  CubeMX owns the pins (GPIO_EXTI, both edges, pull-up) and the us timebase
  *  (TIM6, 1 MHz, free-running). This driver only decodes; the shared
  *  HAL_GPIO_EXTI_Callback dispatcher (main.c) routes each edge to IrRemote_OnEdge.
  ******************************************************************************
  */
#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define IR_REMOTE_RX_COUNT 5

typedef enum
{
  IR_RX_FRONT_L = 0,  /* PB11 */
  IR_RX_FRONT_R,      /* PE15 */
  IR_RX_LEFT,         /* PD13 */
  IR_RX_RIGHT,        /* PE6  */
  IR_RX_REAR          /* PD4  */
} IrRxDir;

/* Latest decoded command + where it was seen. */
typedef struct
{
  uint8_t  valid;      /* a command has been decoded since boot          */
  uint8_t  address;    /* NEC address low byte                           */
  uint8_t  command;    /* NEC command byte                               */
  uint16_t address16;  /* full 16-bit address (extended NEC: lo|hi<<8)   */
  uint8_t  dir_mask;   /* bit IR_RX_* set = that receiver decoded it     */
  uint8_t  repeat;     /* 1 = last update was an NEC repeat (held button)*/
  uint32_t tick;       /* HAL tick of the most recent decode            */
  uint32_t count;      /* total decoded frames (presses + repeats)      */
} IrRemoteEvent;

/* us_timer = a free-running 16-bit timer ticking at 1 MHz (e.g. TIM6, PSC=47,
 * ARR=0xFFFF). Started here; its CNT is sampled in the EXTI ISR for edge timing. */
void        IrRemote_Init(TIM_HandleTypeDef *us_timer);

/* EXTI dispatch hook - called from HAL_GPIO_EXTI_Callback (main.c) per receiver. */
void        IrRemote_OnEdge(IrRxDir ch);

/* Run each main-loop iteration: merges per-channel decodes into the event. */
void        IrRemote_Task(void);

/* Copy the latest event; returns its .valid (0 if nothing decoded yet). */
int         IrRemote_Get(IrRemoteEvent *ev);

/* Short direction label "FL"/"FR"/"L"/"R"/"RR", or 0 if out of range. */
const char *IrRemote_DirName(int dir);

#ifdef __cplusplus
}
#endif

#endif /* IR_REMOTE_H */
