/**
  ******************************************************************************
  * @file    buzzer.h
  * @brief   Passive buzzer (BUZZER1 on PA11 via Q17 current driver) tone +
  *          short 8-bit/chiptune melody player for the V7s Plus robot base.
  *
  *          PA11 = TIM1_CH4 (AF2). The pin lives in V7s_Plus.ioc as a labelled
  *          GPIO output; TIM1 itself is configured here in the driver (CubeMX
  *          silently rejects hand-authored advanced-timer PWM, same as the
  *          front-IR carrier on PB10/TIM2). Non-blocking: call Buzzer_Task()
  *          from the main loop. User-owned.
  ******************************************************************************
  */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

typedef enum
{
  BUZZER_MELODY_BOOT = 0,   /* power-on / ready */
  BUZZER_MELODY_OK,         /* command accepted / success */
  BUZZER_MELODY_ERROR,      /* bad command / fault */
  BUZZER_MELODY_ALERT,      /* obstacle / approach warning */
  BUZZER_MELODY_COIN,       /* playful pickup blip */
  BUZZER_MELODY_COUNT
} BuzzerMelody;

void Buzzer_Init(void);                         /* TIM1_CH4 PWM on PA11 (driver-owned) */
void Buzzer_Play(BuzzerMelody m);               /* start a melody (non-blocking) */
void Buzzer_Tone(uint16_t freq_hz, uint16_t ms);/* single raw tone */
void Buzzer_Stop(void);                         /* silence now */
void Buzzer_Task(void);                          /* advance player; call each main loop */
int  Buzzer_IsPlaying(void);

int          Buzzer_MelodyCount(void);
const char  *Buzzer_MelodyName(int idx);        /* name for melody index, or 0 */
int          Buzzer_MelodyByName(const char *name); /* index or -1 */

#endif /* BUZZER_H */
