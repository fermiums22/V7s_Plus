/**
  ******************************************************************************
  * @file    bumper_hit.h
  * @brief   Impact (bumper-hit) sensors on EXTI - instant, interrupt-driven.
  *
  *  Two optical impact sensors, idle HIGH / active-LOW on a hit:
  *     BUMPER_HIT_LEFT   J17:7 photo OUT -> STM32 PB5
  *     BUMPER_HIT_RIGHT  J2:6  photo OUT -> STM32 PE12
  *
  *  Both are wired to a falling-edge EXTI so an impact is caught the instant it
  *  happens (a wall - or a foot kicking the robot). The ISR ONLY latches a status
  *  flag; it never commands the motors. The motor-control loop polls
  *  BumperHit_Active() at the top of each cycle and brakes immediately, then the
  *  (future) navigation layer decides which way to back off / turn and clears the
  *  latch with BumperHit_Clear(). Pins/EXTI/NVIC owned by CubeMX (MX_GPIO_Init);
  *  the shared HAL_GPIO_EXTI_Callback dispatcher in main.c calls BumperHit_OnEdge().
  ******************************************************************************
  */
#ifndef BUMPER_HIT_H
#define BUMPER_HIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  BUMPER_HIT_LEFT = 0,   /* PB5,  J17:7 */
  BUMPER_HIT_RIGHT,      /* PE12, J2:6  */
  BUMPER_HIT_COUNT
} BumperHitSide;

void     BumperHit_Init(void);
void     BumperHit_OnEdge(uint16_t pin); /* EXTI dispatch hook (called from main.c) */
int      BumperHit_Active(void);     /* 1 if any impact latched since last clear */
uint8_t  BumperHit_Events(void);     /* sticky bitmask: bit BUMPER_HIT_LEFT/RIGHT */
uint32_t BumperHit_LastTick(void);   /* HAL tick of the most recent impact */
void     BumperHit_Clear(void);      /* clear the latch after reacting */
int      BumperHit_Level(int side);  /* instantaneous level (1=idle, 0=pressed) */
const char *BumperHit_Name(int side);

#ifdef __cplusplus
}
#endif

#endif /* BUMPER_HIT_H */
