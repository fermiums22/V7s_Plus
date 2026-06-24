/**
  ******************************************************************************
  * @file    bumper_hit.c
  * @brief   Impact (bumper-hit) sensors on EXTI. User-owned (CubeMX does not own
  *          these pins; EXTI is configured here, like the TIM2 AF on PB10).
  *
  *  PB5  (J17:7) = left  impact photo OUT, idle high, active-low on a hit.
  *  PE12 (J2:6)  = right impact photo OUT, idle high, active-low on a hit.
  *
  *  Both fire a falling-edge interrupt (EXTI5 + EXTI12, both under EXTI4_15 on
  *  STM32F0). The ISR only LATCHES a sticky status flag + timestamp - it never
  *  touches the motors. MotorControl_Task() polls BumperHit_Active() each cycle
  *  and brakes; the navigation layer later inspects the side bitmask and clears.
  ******************************************************************************
  */
#include "main.h"
#include "bumper_hit.h"

#define BUMPER_HIT_LEFT_PIN    GPIO_PIN_5
#define BUMPER_HIT_LEFT_PORT   GPIOB
#define BUMPER_HIT_RIGHT_PIN   GPIO_PIN_12
#define BUMPER_HIT_RIGHT_PORT  GPIOE

static volatile uint8_t  s_events;     /* sticky: bit0 left, bit1 right */
static volatile uint32_t s_last_tick;

void BumperHit_Init(void)
{
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_SYSCFG_CLK_ENABLE();   /* EXTICR lives in SYSCFG (F0) */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  s_events    = 0;
  s_last_tick = 0;

  /* Idle high (pull-up); an impact pulls the line low -> falling-edge IT. */
  g.Mode  = GPIO_MODE_IT_FALLING;
  g.Pull  = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;

  g.Pin = BUMPER_HIT_LEFT_PIN;
  HAL_GPIO_Init(BUMPER_HIT_LEFT_PORT, &g);
  g.Pin = BUMPER_HIT_RIGHT_PIN;
  HAL_GPIO_Init(BUMPER_HIT_RIGHT_PORT, &g);

  /* High priority (preempts the ~1 kHz control work) so the latch is set with
     minimal latency; the handler is tiny. */
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

int      BumperHit_Active(void)    { return (s_events != 0) ? 1 : 0; }
uint8_t  BumperHit_Events(void)    { return s_events; }
uint32_t BumperHit_LastTick(void)  { return s_last_tick; }
void     BumperHit_Clear(void)     { s_events = 0; }

int BumperHit_Level(int side)
{
  if (side == BUMPER_HIT_LEFT)
  {
    return (HAL_GPIO_ReadPin(BUMPER_HIT_LEFT_PORT, BUMPER_HIT_LEFT_PIN) == GPIO_PIN_SET) ? 1 : 0;
  }
  if (side == BUMPER_HIT_RIGHT)
  {
    return (HAL_GPIO_ReadPin(BUMPER_HIT_RIGHT_PORT, BUMPER_HIT_RIGHT_PIN) == GPIO_PIN_SET) ? 1 : 0;
  }
  return 1;
}

const char *BumperHit_Name(int side)
{
  switch (side)
  {
    case BUMPER_HIT_LEFT:  return "L";
    case BUMPER_HIT_RIGHT: return "R";
    default:               return 0;
  }
}

/* HAL EXTI callback (called from EXTI4_15_IRQHandler below). Latch only. */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == BUMPER_HIT_LEFT_PIN)
  {
    s_events |= (uint8_t)(1u << BUMPER_HIT_LEFT);
    s_last_tick = HAL_GetTick();
  }
  else if (pin == BUMPER_HIT_RIGHT_PIN)
  {
    s_events |= (uint8_t)(1u << BUMPER_HIT_RIGHT);
    s_last_tick = HAL_GetTick();
  }
}

/* PB5 = EXTI line 5, PE12 = EXTI line 12 - both vector to EXTI4_15 on STM32F0.
   Each HAL_GPIO_EXTI_IRQHandler only acts on its own pending line, so calling
   both is safe regardless of which pin fired. */
void EXTI4_15_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(BUMPER_HIT_LEFT_PIN);
  HAL_GPIO_EXTI_IRQHandler(BUMPER_HIT_RIGHT_PIN);
}
