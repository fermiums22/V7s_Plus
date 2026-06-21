/**
  ******************************************************************************
  * @file    cliff_ir.h
  * @brief   Downward cliff/floor IR sensors (x4) + the unique left side IR
  *          transceiver (x1) for the V7s Plus robot base. User-owned.
  *
  *  Five reflective IR channels. ALL ride the one shared Q11/PB10 (TIM2 1 kHz)
  *  illumination carrier - the illum-off test confirmed even the LEFT module's
  *  emitters die when Q11 stops (left shunt 37mA -> 0), so there is NO separate
  *  Q22 carrier. All are demodulated in the same synchronous ADC scan
  *  (front_ir_bumper.c); feed each fresh demod result here via CliffIr_SetSignal.
  *
  *     CLIFF_FRONT_L  J4   sense -> STM32 ___ (ADC ch TBD)
  *     CLIFF_FRONT_R  J3   sense -> STM32 ___ (ADC ch TBD)
  *     CLIFF_RIGHT    J2:4 sense -> STM32 PA3 (ADC_IN3)   [CONFIRMED]
  *     CLIFF_LEFT     J17:4 sense -> STM32 ___ (ADC ch TBD)
  *     SIDE_IR        J17:5 sense -> STM32 ___ (ADC ch TBD)
  *
  *  (PE9, once thought to control the left emitters, is actually a voltage-SENSE
  *   input - disproven as a control by the illum-off test - and is not driven.)
  *
  *  Per channel we keep, like the front panel:
  *     signal = lock-in demod amplitude (grows as floor/object gets closer;
  *              a cliff/drop-off makes it fall),
  *     rate   = change since last update (~15 Hz; +approaching, -receding).
  ******************************************************************************
  */
#ifndef CLIFF_IR_H
#define CLIFF_IR_H

#include <stdint.h>

typedef enum
{
  CLIFF_FRONT_L = 0,   /* J4,   carrier = PB10/Q11 */
  CLIFF_FRONT_R,       /* J3,   carrier = PB10/Q11 */
  CLIFF_RIGHT,         /* J2:4 = PA3, carrier = PB10/Q11 */
  CLIFF_LEFT,          /* J17:4, carrier = PB10/Q11 (shared) */
  SIDE_IR,             /* J17:5, carrier = PB10/Q11 (shared) */
  CLIFF_COUNT
} CliffChannel;

/* Live per-channel data (read by console / navigation logic). */
extern volatile int16_t g_cliff_signal[CLIFF_COUNT];
extern volatile int16_t g_cliff_rate[CLIFF_COUNT];

void        CliffIr_Init(void);
void        CliffIr_Task(void);                 /* update rate ~15 Hz; call each main loop */
const char *CliffIr_Name(int ch);               /* short label, or 0 */

/* Front-IR demod (front_ir_bumper.c) calls this to publish a fresh channel
   result (all 5 share the Q11 carrier and the same synchronous ADC scan). */
void CliffIr_SetSignal(int ch, int16_t signal);

/* Right-side main-board DIGITAL sensors whose STM32 pins are confirmed.
   Return the raw pin level (1=high, 0=low); caller interprets polarity. */
int CliffIr_RightHit(void);     /* PE12: right bumper-hit photo output */
int CliffIr_RightBaseIr(void);  /* PE6 : right dock/base IR receiver demod out */

#endif /* CLIFF_IR_H */
