/**
  ******************************************************************************
  * @file    cliff_ir.c
  * @brief   Cliff/floor IR (x4) + left side IR transceiver (x1). User-owned.
  *
  *  SCAFFOLD: the data model, rate engine, console hooks and Q22-carrier control
  *  surface are in place. What is still pending the main-board ring-out:
  *    - GROUP A (PB10 carrier): add the front-cliff sense ADC channels to the
  *      front_ir_bumper.c scan, and call CliffIr_SetSignal(CLIFF_*, demod) for
  *      CLIFF_FRONT_L / CLIFF_FRONT_R / CLIFF_RIGHT (PA3=ADC_IN3 is known).
  *    - GROUP B (Q22 carrier): the STM32 pin that drives Q22, its PWM timer,
  *      and the CLIFF_LEFT / SIDE_IR sense ADC channels.
  *  Until then these read 0; nothing else depends on the missing pins.
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

  /* Right-side digital sensors: PE12 (bumper-hit), PE6 (base IR rx). Both arrive
     through a series R; use internal pull-ups (idle high). */
  {
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();
    g.Pin = GPIO_PIN_12 | GPIO_PIN_6;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &g);
  }
}

int CliffIr_RightHit(void)
{
  return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_12) == GPIO_PIN_SET) ? 1 : 0;
}

int CliffIr_RightBaseIr(void)
{
  return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;
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
