/**
  ******************************************************************************
  * @file    caster_odo.c
  * @brief   Front caster-wheel odometry (J20) on PD2.
  *
  *  U10 (LM393) ch1 = Schmitt trigger off the J20 reflective sensor; its squared
  *  output OUT1 -> PD2. We count every edge (rising+falling) on EXTI2: one count
  *  per black<->white transition of the caster disc. LM393 is open-collector, so
  *  PD2 uses an internal pull-up. The pin/EXTI/NVIC are owned by CubeMX
  *  (MX_GPIO_Init); the shared HAL_GPIO_EXTI_Callback dispatcher in main.c calls
  *  CasterOdo_OnEdge(), which only bumps a counter.
  ******************************************************************************
  */
#include "main.h"
#include "caster_odo.h"

#define CASTER_ODO_PIN   CASTER_ODO_Pin
#define CASTER_ODO_PORT  CASTER_ODO_GPIO_Port

static volatile uint32_t s_count;

void CasterOdo_Init(void)
{
  /* Pin/EXTI/NVIC config is done by CubeMX MX_GPIO_Init; just reset state. */
  s_count = 0;
}

uint32_t CasterOdo_Count(void) { return s_count; }
void     CasterOdo_Reset(void) { s_count = 0; }

int CasterOdo_Level(void)
{
  return (HAL_GPIO_ReadPin(CASTER_ODO_PORT, CASTER_ODO_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

/* Called from the shared HAL_GPIO_EXTI_Callback dispatcher (main.c) on each PD2
 * edge (the pending bit is already cleared by HAL_GPIO_EXTI_IRQHandler). */
void CasterOdo_OnEdge(void)
{
  s_count++;
}
