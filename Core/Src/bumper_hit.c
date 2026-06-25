/**
  ******************************************************************************
  * @file    bumper_hit.c
  * @brief   Impact (bumper-hit) sensors on EXTI.
  *
  *  PB5  (J17:7) = left  impact photo OUT, idle high, active-low on a hit.
  *  PE12 (J2:6)  = right impact photo OUT, idle high, active-low on a hit.
  *
  *  Both fire a falling-edge interrupt (EXTI5 + EXTI12, both under EXTI4_15 on
  *  STM32F0). The pins, pull-ups and NVIC are now owned by CubeMX (MX_GPIO_Init);
  *  the shared HAL_GPIO_EXTI_Callback dispatcher in main.c routes each firing pin
  *  to BumperHit_OnEdge(). The handler only LATCHES a sticky status flag +
  *  timestamp - it never touches the motors. MotorControl_Task() polls
  *  BumperHit_Active() each cycle and brakes; the navigation layer later inspects
  *  the side bitmask and clears.
  ******************************************************************************
  */
#include "main.h"
#include "bumper_hit.h"

#define BUMPER_HIT_LEFT_PIN    BUMPER_HIT_LEFT_Pin
#define BUMPER_HIT_LEFT_PORT   BUMPER_HIT_LEFT_GPIO_Port
#define BUMPER_HIT_RIGHT_PIN   BUMPER_HIT_RIGHT_Pin
#define BUMPER_HIT_RIGHT_PORT  BUMPER_HIT_RIGHT_GPIO_Port

static volatile uint8_t  s_events;     /* sticky: bit0 left, bit1 right */
static volatile uint32_t s_last_tick;

void BumperHit_Init(void)
{
  /* Pin/EXTI/NVIC config is done by CubeMX MX_GPIO_Init; just reset state. */
  s_events    = 0;
  s_last_tick = 0;
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

/* Called from the shared HAL_GPIO_EXTI_Callback dispatcher (main.c) with the
   firing pin. Latch only - never touches the motors. */
void BumperHit_OnEdge(uint16_t pin)
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
