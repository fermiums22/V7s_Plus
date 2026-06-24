/**
  ******************************************************************************
  * @file    base_ir.h
  * @brief   Dock/base IR-beacon receivers (x5) - direction finding for homing.
  *
  *  Five demodulated IR receivers look at the charging dock's modulated beacon.
  *  Each output idles HIGH and pulses LOW while it sees the beacon, so the
  *  fraction of low-time over a short window = how strongly that direction sees
  *  the dock. Unlike the reflective sensors these need NO emitter/carrier of our
  *  own - they listen to the dock. Polled (no interrupt needed) in the main loop.
  *
  *     BASE_IR_FRONT_L  on-board front-left  -> PB11
  *     BASE_IR_FRONT_R  on-board front-right -> PE15
  *     BASE_IR_LEFT     J17:6 (left side)    -> PD13
  *     BASE_IR_RIGHT    J2:5  (right side)   -> PE6
  *     BASE_IR_REAR     J15:3 (rear)         -> PD4
  *
  *  All 5 pins are declared as GPIO inputs (pull-up) in V7s_Plus.ioc.
  ******************************************************************************
  */
#ifndef BASE_IR_H
#define BASE_IR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  BASE_IR_FRONT_L = 0,  /* PB11 */
  BASE_IR_FRONT_R,      /* PE15 */
  BASE_IR_LEFT,         /* PD13, J17:6 */
  BASE_IR_RIGHT,        /* PE6,  J2:5  */
  BASE_IR_REAR,         /* PD4,  J15:3 */
  BASE_IR_COUNT
} BaseIrDir;

/* Live per-direction data (read by console / homing logic). */
extern volatile uint8_t  g_base_ir_level[BASE_IR_COUNT];    /* last raw level (1 idle, 0 active-low) */
extern volatile uint16_t g_base_ir_activity[BASE_IR_COUNT]; /* low-time over last window, per-mille (0..1000) */
extern volatile uint8_t  g_base_ir_seen[BASE_IR_COUNT];     /* 1 = beacon seen this window */

void        BaseIr_Init(void);
void        BaseIr_Task(void);       /* sample fast; call each main loop */
const char *BaseIr_Name(int dir);    /* short label, or 0 */
int         BaseIr_Direction(void);  /* strongest seen direction, or -1 if none */

#ifdef __cplusplus
}
#endif

#endif /* BASE_IR_H */
