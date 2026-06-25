/**
  ******************************************************************************
  * @file    ir_remote.c
  * @brief   NEC decode on the 5 IR receivers + direction-of-arrival. User-owned
  *          (decode logic only; pins/timer are CubeMX-owned). See ir_remote.h.
  ******************************************************************************
  */
#include "ir_remote.h"

/* NEC mark/space widths in microseconds, with generous (+-~30%) windows so a
 * cheap receiver's jitter still decodes. Receiver idles HIGH; a burst pulls it
 * LOW, so MARK = LOW duration, SPACE = HIGH duration. */
#define NEC_LEAD_MARK_MIN   8000u
#define NEC_LEAD_MARK_MAX  10000u
#define NEC_LEAD_SPACE_MIN  3800u   /* 4.5 ms start-of-data space */
#define NEC_LEAD_SPACE_MAX  5200u
#define NEC_RPT_SPACE_MIN   1900u   /* 2.25 ms repeat space       */
#define NEC_RPT_SPACE_MAX   2700u
#define NEC_BIT_MARK_MIN     350u   /* 560 us bit mark            */
#define NEC_BIT_MARK_MAX     800u
#define NEC_BIT0_SPACE_MIN   350u   /* 560 us  -> logical 0       */
#define NEC_BIT0_SPACE_MAX   800u
#define NEC_BIT1_SPACE_MIN  1300u   /* 1690 us -> logical 1       */
#define NEC_BIT1_SPACE_MAX  2000u

/* Receivers seeing the same press decode within a few ms of each other; merge
 * them into one event. NEC repeats arrive ~108 ms apart, so a window well under
 * that (but above multi-receiver jitter) starts a fresh dir_mask per repeat -
 * i.e. dir_mask always reflects who sees the press right now. */
#define IR_COINCIDENCE_MS   60u

enum { NEC_IDLE = 0, NEC_LEAD, NEC_DATA };

typedef struct
{
  uint8_t  state;
  uint16_t last_us;     /* timer CNT at the previous edge */
  uint8_t  bit_idx;
  uint32_t bits;        /* accumulated LSB-first           */
  /* result pending for IrRemote_Task() + persistent last-good for repeats */
  volatile uint8_t  have;
  uint8_t  r_addr;
  uint8_t  r_cmd;
  uint16_t r_addr16;
  uint8_t  r_repeat;
  uint32_t r_tick;
} NecCh;

static TIM_HandleTypeDef *s_tim;
static NecCh              s_ch[IR_REMOTE_RX_COUNT];
static IrRemoteEvent      s_event;

static GPIO_TypeDef *const s_port[IR_REMOTE_RX_COUNT] = {
  BASE_IR_FRONT_L_GPIO_Port, BASE_IR_FRONT_R_GPIO_Port, BASE_IR_LEFT_GPIO_Port,
  BASE_IR_RIGHT_GPIO_Port,   BASE_IR_REAR_GPIO_Port,
};
static const uint16_t s_pin[IR_REMOTE_RX_COUNT] = {
  BASE_IR_FRONT_L_Pin, BASE_IR_FRONT_R_Pin, BASE_IR_LEFT_Pin,
  BASE_IR_RIGHT_Pin,   BASE_IR_REAR_Pin,
};
static const char *const s_name[IR_REMOTE_RX_COUNT] = { "FL", "FR", "L", "R", "RR" };

static int in_win(uint16_t w, uint16_t lo, uint16_t hi)
{
  return (w >= lo && w <= hi) ? 1 : 0;
}

void IrRemote_Init(TIM_HandleTypeDef *us_timer)
{
  int i;

  s_tim = us_timer;
  for (i = 0; i < IR_REMOTE_RX_COUNT; i++)
  {
    s_ch[i].state = NEC_IDLE;
    s_ch[i].have  = 0;
  }
  s_event.valid = 0;
  s_event.count = 0;

  if (s_tim != 0)
  {
    HAL_TIM_Base_Start(s_tim);   /* free-running 1 MHz timebase for edge widths */
  }
}

const char *IrRemote_DirName(int dir)
{
  if (dir < 0 || dir >= IR_REMOTE_RX_COUNT)
  {
    return 0;
  }
  return s_name[dir];
}

/* Finish a 32-bit frame: NEC byte order (LSB-first) is addr, ~addr/addrHi, cmd,
 * ~cmd. The command-complement check is the reliable integrity test (holds for
 * both standard and extended NEC). */
static void nec_complete(NecCh *c)
{
  uint8_t b0 = (uint8_t)(c->bits      );
  uint8_t b1 = (uint8_t)(c->bits >>  8);
  uint8_t b2 = (uint8_t)(c->bits >> 16);
  uint8_t b3 = (uint8_t)(c->bits >> 24);

  if (b2 == (uint8_t)(~b3))
  {
    c->r_addr   = b0;
    c->r_cmd    = b2;
    c->r_addr16 = (uint16_t)(b0 | (b1 << 8));
    c->r_repeat = 0;
    c->r_tick   = HAL_GetTick();
    c->have     = 1;
  }
  c->state = NEC_IDLE;
}

/* A LOW burst (MARK) of width w just ended (rising edge). */
static void nec_mark(NecCh *c, uint16_t w)
{
  if (in_win(w, NEC_LEAD_MARK_MIN, NEC_LEAD_MARK_MAX))
  {
    c->state = NEC_LEAD;            /* leader mark -> expect the leader space */
    return;
  }
  if (c->state == NEC_DATA && in_win(w, NEC_BIT_MARK_MIN, NEC_BIT_MARK_MAX))
  {
    return;                        /* normal bit mark; the value rides the space */
  }
  c->state = NEC_IDLE;             /* anything else = noise */
}

/* A HIGH gap (SPACE) of width w just ended (falling edge). */
static void nec_space(NecCh *c, uint16_t w)
{
  if (c->state == NEC_LEAD)
  {
    if (in_win(w, NEC_LEAD_SPACE_MIN, NEC_LEAD_SPACE_MAX))
    {
      c->state   = NEC_DATA;       /* 4.5 ms -> data follows */
      c->bit_idx = 0;
      c->bits    = 0;
    }
    else if (in_win(w, NEC_RPT_SPACE_MIN, NEC_RPT_SPACE_MAX))
    {
      c->r_repeat = 1;             /* 2.25 ms -> repeat of the last command */
      c->r_tick   = HAL_GetTick();
      c->have     = 1;
      c->state    = NEC_IDLE;
    }
    else
    {
      c->state = NEC_IDLE;
    }
    return;
  }

  if (c->state == NEC_DATA)
  {
    if (in_win(w, NEC_BIT0_SPACE_MIN, NEC_BIT0_SPACE_MAX))
    {
      c->bit_idx++;                /* logical 0: leave the bit clear */
    }
    else if (in_win(w, NEC_BIT1_SPACE_MIN, NEC_BIT1_SPACE_MAX))
    {
      c->bits |= (1UL << c->bit_idx);
      c->bit_idx++;
    }
    else
    {
      c->state = NEC_IDLE;
      return;
    }
    if (c->bit_idx >= 32u)
    {
      nec_complete(c);
    }
  }
}

void IrRemote_OnEdge(IrRxDir ch)
{
  NecCh   *c;
  uint16_t now;
  uint16_t w;

  if ((unsigned)ch >= IR_REMOTE_RX_COUNT || s_tim == 0)
  {
    return;
  }

  c   = &s_ch[ch];
  now = (uint16_t)__HAL_TIM_GET_COUNTER(s_tim);
  w   = (uint16_t)(now - c->last_us);   /* width of the interval that just ended */
  c->last_us = now;

  /* Level AFTER the edge: HIGH => we just finished a LOW mark; LOW => a HIGH space. */
  if (HAL_GPIO_ReadPin(s_port[ch], s_pin[ch]) == GPIO_PIN_SET)
  {
    nec_mark(c, w);
  }
  else
  {
    nec_space(c, w);
  }
}

void IrRemote_Task(void)
{
  uint32_t now = HAL_GetTick();
  int ch;

  for (ch = 0; ch < IR_REMOTE_RX_COUNT; ch++)
  {
    NecCh  *c = &s_ch[ch];
    uint8_t addr, cmd, rep;
    uint16_t a16;

    if (!c->have)
    {
      continue;
    }

    /* Snapshot + clear under a brief mask (ISR may set .have again any moment). */
    __disable_irq();
    addr = c->r_addr;
    cmd  = c->r_cmd;
    rep  = c->r_repeat;
    a16  = c->r_addr16;
    c->have = 0;
    __enable_irq();

    if (s_event.valid && (now - s_event.tick) <= IR_COINCIDENCE_MS &&
        cmd == s_event.command && addr == s_event.address)
    {
      /* same press seen by another receiver (or a fast repeat): widen the mask */
      s_event.dir_mask |= (uint8_t)(1u << ch);
      if (rep)
      {
        s_event.repeat = 1;
      }
    }
    else
    {
      /* new press, or a held-button repeat after the coincidence gap */
      s_event.address   = addr;
      s_event.command   = cmd;
      s_event.address16 = a16;
      s_event.dir_mask  = (uint8_t)(1u << ch);
      s_event.repeat    = rep;
    }
    s_event.tick  = now;
    s_event.valid = 1;
    s_event.count++;
  }
}

int IrRemote_Get(IrRemoteEvent *ev)
{
  if (ev == 0)
  {
    return 0;
  }
  __disable_irq();
  *ev = s_event;
  __enable_irq();
  return ev->valid;
}
