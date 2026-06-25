/**
  ******************************************************************************
  * @file    buzzer.c
  * @brief   Passive buzzer tone + short 8-bit melody player. User-owned.
  *
  *          Hardware: BUZZER1 driven by Q17 (current driver), RC-coupled to
  *          PA11. PA11 = TIM1_CH4 (AF2). We drive a 50%-duty square wave at the
  *          note frequency; the RC coupling passes the AC edges into Q17.
  *
  *          TIM1 is clocked from PCLK = 48 MHz. PSC = 47 -> 1 MHz tick, so for a
  *          note frequency f: ARR = 1e6/f - 1, CCR4 = (ARR+1)/2 (50% duty).
  ******************************************************************************
  */
#include "main.h"
#include "buzzer.h"

/* ---- tone generator (TIM1_CH4 PWM on PA11) -------------------------------- */
/* TIM1 (PSC 47 -> 1 MHz tick, CH4 PWM on PA11 = AF2) is configured by CubeMX
 * MX_TIM1_Init; this driver only retunes ARR/CCR4 per note and starts/stops the
 * channel. PA11 routing is done by HAL_TIM_MspPostInit. */

#define BUZZER_TIM_TICK_HZ   1000000u   /* PSC 47 on 48 MHz PCLK */

extern TIM_HandleTypeDef htim1;

static void buzzer_tone_off(void)
{
  HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_4);
}

static void buzzer_tone_on(uint16_t freq_hz)
{
  uint32_t arr;

  if (freq_hz == 0u)
  {
    buzzer_tone_off();
    return;
  }

  arr = BUZZER_TIM_TICK_HZ / freq_hz;
  if (arr < 2u)      arr = 2u;
  if (arr > 65535u)  arr = 65535u;
  arr -= 1u;

  __HAL_TIM_SET_AUTORELOAD(&htim1, arr);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, (arr + 1u) / 2u); /* 50% */
  __HAL_TIM_SET_COUNTER(&htim1, 0u);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4); /* enables MOE for TIM1 */
}

void Buzzer_Init(void)
{
  /* TIM1_CH4 / PA11 are owned by CubeMX (MX_TIM1_Init); just ensure silence. */
  buzzer_tone_off();
}

/* ---- note table & melodies ----------------------------------------------- */
/* index 0 = rest; C4..C6. Square-wave tones are inherently "8-bit"/chiptune.  */
static const uint16_t NOTE_FREQ[] =
{
  0,                                            /* 0  rest      */
  262, 294, 330, 349, 392, 440, 494,            /* 1..7  C4..B4 */
  523, 587, 659, 698, 784, 880, 988,            /* 8..14 C5..B5 */
  1047                                          /* 15   C6      */
};

/* Flat {note, duration} pairs; duration in units of 10 ms (so 1 byte each).   */
static const uint8_t MEL_BOOT[]  = { 8,8, 10,8, 12,8, 15,16 };          /* C5 E5 G5 C6 up */
static const uint8_t MEL_OK[]    = { 12,6, 15,12 };                     /* G5 C6 */
static const uint8_t MEL_ERROR[] = { 6,10, 0,3, 4,20 };                 /* A4 - F4 (down) */
static const uint8_t MEL_ALERT[] = { 15,7, 0,5, 15,7, 0,5, 15,7 };      /* C6 beep x3 */
static const uint8_t MEL_COIN[]  = { 13,5, 15,28 };                     /* A5 -> C6 blip */

typedef struct
{
  const uint8_t *data;   /* {note,dur10ms} pairs */
  uint8_t        notes;  /* pair count */
  const char    *name;
} Melody;

static const Melody MELODIES[BUZZER_MELODY_COUNT] =
{
  { MEL_BOOT,  (uint8_t)(sizeof(MEL_BOOT)  / 2), "boot"  },
  { MEL_OK,    (uint8_t)(sizeof(MEL_OK)    / 2), "ok"    },
  { MEL_ERROR, (uint8_t)(sizeof(MEL_ERROR) / 2), "error" },
  { MEL_ALERT, (uint8_t)(sizeof(MEL_ALERT) / 2), "alert" },
  { MEL_COIN,  (uint8_t)(sizeof(MEL_COIN)  / 2), "coin"  },
};

/* ---- non-blocking player -------------------------------------------------- */

static const uint8_t *s_seq;        /* current melody data, 0 = raw tone */
static uint8_t        s_len;        /* note pairs */
static uint8_t        s_idx;        /* next note */
static uint8_t        s_playing;
static uint32_t       s_note_end;   /* HAL tick when current step ends */

void Buzzer_Play(BuzzerMelody m)
{
  if ((unsigned)m >= (unsigned)BUZZER_MELODY_COUNT)
  {
    return;
  }
  s_seq = MELODIES[m].data;
  s_len = MELODIES[m].notes;
  s_idx = 0u;
  s_playing = 1u;
  s_note_end = HAL_GetTick();   /* due now -> Task loads note 0 immediately */
  Buzzer_Task();
}

void Buzzer_Tone(uint16_t freq_hz, uint16_t ms)
{
  buzzer_tone_on(freq_hz);
  s_seq = 0;
  s_len = 0u;
  s_idx = 0u;
  s_playing = 1u;
  s_note_end = HAL_GetTick() + (uint32_t)ms;
}

void Buzzer_Stop(void)
{
  buzzer_tone_off();
  s_playing = 0u;
}

void Buzzer_Task(void)
{
  uint8_t note;
  uint8_t dur10;

  if (!s_playing)
  {
    return;
  }
  if ((int32_t)(HAL_GetTick() - s_note_end) < 0)
  {
    return;                       /* current note/tone still sounding */
  }

  /* raw tone (no sequence) or sequence finished -> stop */
  if (s_seq == 0 || s_idx >= s_len)
  {
    buzzer_tone_off();
    s_playing = 0u;
    return;
  }

  note  = s_seq[(uint16_t)s_idx * 2u];
  dur10 = s_seq[(uint16_t)s_idx * 2u + 1u];
  s_idx++;

  buzzer_tone_on((note < (sizeof(NOTE_FREQ) / sizeof(NOTE_FREQ[0]))) ? NOTE_FREQ[note] : 0u);
  s_note_end = HAL_GetTick() + (uint32_t)dur10 * 10u;
}

int Buzzer_IsPlaying(void)
{
  return (int)s_playing;
}

int Buzzer_MelodyCount(void)
{
  return BUZZER_MELODY_COUNT;
}

const char *Buzzer_MelodyName(int idx)
{
  if (idx < 0 || idx >= BUZZER_MELODY_COUNT)
  {
    return 0;
  }
  return MELODIES[idx].name;
}

int Buzzer_MelodyByName(const char *name)
{
  int i;

  if (name == 0)
  {
    return -1;
  }
  for (i = 0; i < BUZZER_MELODY_COUNT; i++)
  {
    const char *a = MELODIES[i].name;
    const char *b = name;
    while (*a && *b && *a == *b)
    {
      a++;
      b++;
    }
    if (*a == 0 && *b == 0)
    {
      return i;
    }
  }
  return -1;
}
