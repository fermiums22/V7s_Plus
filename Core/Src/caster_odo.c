/**
  ******************************************************************************
  * @file    caster_odo.c
  * @brief   Front caster-wheel odometry (J20) on PD2. User-owned.
  *
  *  U10 (LM393) ch1 = Schmitt trigger off the J20 reflective sensor; its squared
  *  output OUT1 -> PD2. We count every edge (rising+falling) on EXTI2: one count
  *  per black<->white transition of the caster disc. LM393 is open-collector, so
  *  PD2 uses an internal pull-up. The ISR only bumps a counter.
  ******************************************************************************
  */
#include "main.h"
#include "caster_odo.h"

#define CASTER_ODO_PIN   GPIO_PIN_2
#define CASTER_ODO_PORT  GPIOD

static volatile uint32_t s_count;

void CasterOdo_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_SYSCFG_CLK_ENABLE();   /* EXTICR lives in SYSCFG (F0) */
  __HAL_RCC_GPIOD_CLK_ENABLE();

  s_count = 0;

  g.Pin   = CASTER_ODO_PIN;
  g.Mode  = GPIO_MODE_IT_RISING_FALLING;  /* count every black/white transition */
  g.Pull  = GPIO_PULLUP;                  /* U10 OUT1 is open-collector */
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CASTER_ODO_PORT, &g);

  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
}

uint32_t CasterOdo_Count(void) { return s_count; }
void     CasterOdo_Reset(void) { s_count = 0; }

int CasterOdo_Level(void)
{
  return (HAL_GPIO_ReadPin(CASTER_ODO_PORT, CASTER_ODO_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

/* PD2 = EXTI line 2 -> EXTI2_3 vector on STM32F0. Handle the line directly here
 * (no HAL_GPIO_EXTI_Callback, which bumper_hit.c owns for EXTI4_15). */
void EXTI2_3_IRQHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(CASTER_ODO_PIN) != 0U)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(CASTER_ODO_PIN);
    s_count++;
  }
}
