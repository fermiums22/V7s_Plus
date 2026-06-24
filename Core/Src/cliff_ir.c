/**
  ******************************************************************************
  * @file    cliff_ir.c
  * @brief   Cliff/floor IR (x4) + left side IR transceiver (x1). User-owned.
  *
  *  All 5 sense lines are now mapped and live: front_ir_bumper.c scans the 4
  *  cliff ADC channels (FL=PC2/IN12, FR=PC4/IN14, RR=PA3/IN3, LL=PC1/IN11) plus
  *  the side transceiver (SIDE=PC0/IN10) in its 8-channel synchronous demod and
  *  publishes each here via CliffIr_SetSignal. There is NO separate Q22 carrier -
  *  the illum-off test proved every emitter (incl. left cliff + side) rides the
  *  shared Q11/PB10 carrier, so all 5 demodulate against it just like the panel.
  ******************************************************************************
  */
#include "main.h"
#include "cliff_ir.h"

/* ---- live data ----------------------------------------------------------- */

volatile int16_t g_cliff_signal[CLIFF_COUNT];
volatile int16_t g_cliff_rate[CLIFF_COUNT];

static int16_t s_prev[CLIFF_COUNT];

static const char *const CLIFF_NAME[CLIFF_COUNT] =
{
  "FL",    /* front-left  cliff */
  "FR",    /* front-right cliff */
  "RR",    /* right       cliff */
  "LL",    /* left        cliff */
  "SIDE"   /* side IR transceiver */
};

/* rate refresh cadence (matches the front panel feel) */
#define CLIFF_RATE_PERIOD_MS   66u      /* ~15 Hz */
static uint32_t s_rate_last;

/* ---- API ----------------------------------------------------------------- */

void CliffIr_Init(void)
{
  int i;
  for (i = 0; i < CLIFF_COUNT; i++)
  {
    g_cliff_signal[i] = 0;
    g_cliff_rate[i]   = 0;
    s_prev[i]         = 0;
  }
  s_rate_last = HAL_GetTick();

  /* All emitters (incl. left cliff + side) ride the shared Q11/PB10 carrier -
     confirmed by the illum-off test (left shunt 37mA -> 0 when Q11 stopped).
     There is NO separate Q22 carrier. PE9 is a voltage-SENSE input, not a
     control, so it is deliberately left unconfigured (not driven). */

  /* The right-side digital sensors that used to live here have moved out:
     the bumper-hit inputs (PE12 + PB5) are now EXTI-driven in bumper_hit.c, and
     the dock/base IR receivers (incl. PE6) are polled in base_ir.c. */
}

void CliffIr_SetSignal(int ch, int16_t signal)
{
  if (ch < 0 || ch >= CLIFF_COUNT)
  {
    return;
  }
  g_cliff_signal[ch] = signal;
}

void CliffIr_Task(void)
{
  uint32_t now = HAL_GetTick();
  int i;

  if ((now - s_rate_last) < CLIFF_RATE_PERIOD_MS)
  {
    return;
  }
  s_rate_last = now;

  for (i = 0; i < CLIFF_COUNT; i++)
  {
    int16_t sig = g_cliff_signal[i];
    g_cliff_rate[i] = (int16_t)(sig - s_prev[i]);
    s_prev[i] = sig;
  }
}

const char *CliffIr_Name(int ch)
{
  if (ch < 0 || ch >= CLIFF_COUNT)
  {
    return 0;
  }
  return CLIFF_NAME[ch];
}

/* ---- note on the left side -----------------------------------------------
 * Earlier this module drove PE9 as a TIM1_CH1 PWM, on the theory that PE9 ->
 * Q30 -> Q23 -> Q22 enabled the left emitters. The illum-off test DISPROVED
 * that: the left emitters are fed by the shared Q11/PB10 carrier (stopping Q11
 * dropped the left 6R90-shunt current from 37 mA to 0). PE9 is instead a
 * voltage-SENSE input. So there is no Q22 carrier to drive here, and PE9 is
 * left as an input (unconfigured). The left cliff (J17:4) and side (J17:5)
 * sense lines therefore demodulate against the same Q11 carrier as GROUP A.
 * -------------------------------------------------------------------------- */
