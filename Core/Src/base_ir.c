/**
  ******************************************************************************
  * @file    base_ir.c
  * @brief   Dock/base IR-beacon receivers (x5), polled. User-owned.
  *
  *  Each receiver's demod output idles HIGH and pulses LOW while the dock beacon
  *  is in view. We sample all 5 as fast as the main loop runs and, every
  *  BASE_IR_WINDOW_MS, reduce each channel to a low-time ratio in per-mille
  *  (robust to a varying loop rate). That ratio is the per-direction beacon
  *  strength; the strongest "seen" direction points roughly at the dock.
  *
  *  GPIO config (input + pull-up) is done by the CubeMX-generated MX_GPIO_Init
  *  from the .ioc; this module only reads the pins.
  ******************************************************************************
  */
#include "main.h"
#include "base_ir.h"

#define BASE_IR_WINDOW_MS      20u   /* integrate low-time over this window */
#define BASE_IR_SEEN_PERMILLE  40u   /* >=4% low-time in the window = beacon seen */

volatile uint8_t  g_base_ir_level[BASE_IR_COUNT];
volatile uint16_t g_base_ir_activity[BASE_IR_COUNT];
volatile uint8_t  g_base_ir_seen[BASE_IR_COUNT];

static const struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  const char   *name;
} s_ch[BASE_IR_COUNT] =
{
  { BASE_IR_FRONT_L_GPIO_Port, BASE_IR_FRONT_L_Pin, "FL" },
  { BASE_IR_FRONT_R_GPIO_Port, BASE_IR_FRONT_R_Pin, "FR" },
  { BASE_IR_LEFT_GPIO_Port,    BASE_IR_LEFT_Pin,    "L"  },
  { BASE_IR_RIGHT_GPIO_Port,   BASE_IR_RIGHT_Pin,   "R"  },
  { BASE_IR_REAR_GPIO_Port,    BASE_IR_REAR_Pin,    "RR" },
};

static uint32_t s_low_cnt[BASE_IR_COUNT];
static uint32_t s_samples;
static uint32_t s_win_start;

void BaseIr_Init(void)
{
  int i;
  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    g_base_ir_level[i]    = 1;
    g_base_ir_activity[i] = 0;
    g_base_ir_seen[i]     = 0;
    s_low_cnt[i]          = 0;
  }
  s_samples   = 0;
  s_win_start = HAL_GetTick();
}

void BaseIr_Task(void)
{
  uint32_t now;
  uint32_t n;
  int i;

  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    int lvl = (HAL_GPIO_ReadPin(s_ch[i].port, s_ch[i].pin) == GPIO_PIN_SET) ? 1 : 0;
    g_base_ir_level[i] = (uint8_t)lvl;
    if (!lvl)
    {
      s_low_cnt[i]++;
    }
  }
  s_samples++;

  now = HAL_GetTick();
  if ((now - s_win_start) < BASE_IR_WINDOW_MS)
  {
    return;
  }

  n = s_samples ? s_samples : 1u;
  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    uint16_t permille = (uint16_t)((s_low_cnt[i] * 1000u) / n);
    g_base_ir_activity[i] = permille;
    g_base_ir_seen[i]     = (permille >= BASE_IR_SEEN_PERMILLE) ? 1u : 0u;
    s_low_cnt[i]          = 0;
  }
  s_samples   = 0;
  s_win_start = now;
}

const char *BaseIr_Name(int dir)
{
  if (dir < 0 || dir >= BASE_IR_COUNT)
  {
    return 0;
  }
  return s_ch[dir].name;
}

int BaseIr_Direction(void)
{
  int best = -1;
  uint16_t bestv = 0;
  int i;
  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    if (g_base_ir_seen[i] && (g_base_ir_activity[i] > bestv))
    {
      bestv = g_base_ir_activity[i];
      best  = i;
    }
  }
  return best;
}
