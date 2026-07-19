/**
  ******************************************************************************
  * @file    base_ir.c
  * @brief   Dock/base IR-beacon receivers (x5), protocol decoder. User-owned.
  *
  *  The decoder is calibrated from PB11 captures made against the real dock.
  *  A valid frame is an approximately 9 ms LOW leader, 4.5 ms HIGH leader,
  *  seven LSB-first pulse-distance bits, and a final short LOW mark. The dock
  *  alternates command 0x18 and 0x20. Only complete frames carrying one of
  *  those commands contribute to presence or direction confidence.
  *
  *  GPIO config (input + pull-up) is done by the CubeMX-generated MX_GPIO_Init
  *  from the .ioc; this module only reads the pins.
  ******************************************************************************
  */
#include "main.h"
#include "base_ir.h"

#define BASE_IR_SCORE_WINDOW_MS       100u
#define BASE_IR_RELEASE_MS            350u
#define BASE_IR_ASSERT_FRAMES           2u
#define BASE_IR_DIRECTION_MARGIN       100u
#define BASE_IR_DIRECTION_WINDOWS        3u

/* Measured on the real dock (PB11, TIM6 at 1 MHz):
 *   leader LOW  9052..9113 us, leader HIGH 4421..4598 us
 *   data LOW     488..778 us, zero HIGH     389..614 us
 *   one HIGH    1607..1708 us, inter-frame gap about 25.4 ms.
 * Bounds include margin for receiver and oscillator variation. */
#define BASE_IR_LEADER_MARK_MIN_US    8000u
#define BASE_IR_LEADER_MARK_MAX_US   10000u
#define BASE_IR_LEADER_SPACE_MIN_US   3800u
#define BASE_IR_LEADER_SPACE_MAX_US   5200u
#define BASE_IR_DATA_MARK_MIN_US       300u
#define BASE_IR_DATA_MARK_MAX_US       950u
#define BASE_IR_ZERO_SPACE_MIN_US      250u
#define BASE_IR_ZERO_SPACE_MAX_US      950u
#define BASE_IR_ONE_SPACE_MIN_US      1200u
#define BASE_IR_ONE_SPACE_MAX_US      2100u
#define BASE_IR_COMMAND_A              0x18u
#define BASE_IR_COMMAND_B              0x20u
#define BASE_IR_DATA_BITS                 7u

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

typedef enum
{
  RX_WAIT_LEADER = 0,
  RX_WAIT_LEADER_SPACE,
  RX_WAIT_DATA_MARK,
  RX_WAIT_DATA_SPACE,
  RX_WAIT_STOP_MARK
} BaseIrRxState;

static volatile uint16_t s_valid_frames[BASE_IR_COUNT];
static volatile uint32_t s_last_frame_ms[BASE_IR_COUNT];
static uint16_t s_last_edge_us[BASE_IR_COUNT];
static uint8_t s_edge_timed[BASE_IR_COUNT];
static uint8_t s_rx_state[BASE_IR_COUNT];
static uint8_t s_rx_code[BASE_IR_COUNT];
static uint8_t s_rx_bits[BASE_IR_COUNT];
static uint8_t s_presence_frames[BASE_IR_COUNT];
static uint32_t s_win_start;
static int8_t s_direction;
static int8_t s_direction_candidate;
static uint8_t s_direction_windows;

static uint8_t in_range(uint16_t value, uint16_t minimum, uint16_t maximum)
{
  return (value >= minimum && value <= maximum) ? 1u : 0u;
}

static uint8_t valid_command(uint8_t command)
{
  return (command == BASE_IR_COMMAND_A || command == BASE_IR_COMMAND_B) ? 1u : 0u;
}

static void accept_frame(BaseIrDir dir)
{
  if (!valid_command(s_rx_code[dir]))
  {
    return;
  }

  if (s_valid_frames[dir] != 0xFFFFu)
  {
    s_valid_frames[dir]++;
  }
  s_last_frame_ms[dir] = HAL_GetTick();
}

void BaseIr_Init(void)
{
  int i;
  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    g_base_ir_level[i]    = 1;
    g_base_ir_activity[i] = 0;
    g_base_ir_seen[i]     = 0;
    s_valid_frames[i]     = 0u;
    s_last_frame_ms[i]    = 0u;
    s_last_edge_us[i]     = 0u;
    s_edge_timed[i]       = 0u;
    s_rx_state[i]         = RX_WAIT_LEADER;
    s_rx_code[i]          = 0u;
    s_rx_bits[i]          = 0u;
    s_presence_frames[i]  = 0u;
  }
  s_win_start = HAL_GetTick();
  s_direction = -1;
  s_direction_candidate = -1;
  s_direction_windows = 0u;
}

void BaseIr_OnEdge(BaseIrDir dir, uint8_t level_after, uint16_t timestamp_us)
{
  uint16_t elapsed;

  if ((unsigned)dir >= BASE_IR_COUNT || level_after > 1u)
  {
    return;
  }

  g_base_ir_level[dir] = level_after;
  if (!s_edge_timed[dir])
  {
    s_last_edge_us[dir] = timestamp_us;
    s_edge_timed[dir] = 1u;
    return;
  }

  elapsed = (uint16_t)(timestamp_us - s_last_edge_us[dir]);
  s_last_edge_us[dir] = timestamp_us;

  /* A leader LOW mark can re-synchronise the decoder from any state after a
   * dropped edge. A rising edge means the just-ended interval was LOW. */
  if (level_after != 0u &&
      in_range(elapsed, BASE_IR_LEADER_MARK_MIN_US, BASE_IR_LEADER_MARK_MAX_US))
  {
    s_rx_state[dir] = RX_WAIT_LEADER_SPACE;
    s_rx_code[dir] = 0u;
    s_rx_bits[dir] = 0u;
    return;
  }

  switch ((BaseIrRxState)s_rx_state[dir])
  {
    case RX_WAIT_LEADER_SPACE:
      if (level_after == 0u &&
          in_range(elapsed, BASE_IR_LEADER_SPACE_MIN_US, BASE_IR_LEADER_SPACE_MAX_US))
      {
        s_rx_state[dir] = RX_WAIT_DATA_MARK;
      }
      else
      {
        s_rx_state[dir] = RX_WAIT_LEADER;
      }
      break;

    case RX_WAIT_DATA_MARK:
      if (level_after != 0u &&
          in_range(elapsed, BASE_IR_DATA_MARK_MIN_US, BASE_IR_DATA_MARK_MAX_US))
      {
        s_rx_state[dir] = RX_WAIT_DATA_SPACE;
      }
      else
      {
        s_rx_state[dir] = RX_WAIT_LEADER;
      }
      break;

    case RX_WAIT_DATA_SPACE:
      if (level_after != 0u)
      {
        s_rx_state[dir] = RX_WAIT_LEADER;
      }
      else if (in_range(elapsed, BASE_IR_ZERO_SPACE_MIN_US, BASE_IR_ZERO_SPACE_MAX_US))
      {
        s_rx_bits[dir]++;
        s_rx_state[dir] = (s_rx_bits[dir] >= BASE_IR_DATA_BITS) ?
                          RX_WAIT_STOP_MARK : RX_WAIT_DATA_MARK;
      }
      else if (in_range(elapsed, BASE_IR_ONE_SPACE_MIN_US, BASE_IR_ONE_SPACE_MAX_US))
      {
        s_rx_code[dir] |= (uint8_t)(1u << s_rx_bits[dir]);
        s_rx_bits[dir]++;
        s_rx_state[dir] = (s_rx_bits[dir] >= BASE_IR_DATA_BITS) ?
                          RX_WAIT_STOP_MARK : RX_WAIT_DATA_MARK;
      }
      else
      {
        s_rx_state[dir] = RX_WAIT_LEADER;
      }
      break;

    case RX_WAIT_STOP_MARK:
      if (level_after != 0u &&
          in_range(elapsed, BASE_IR_DATA_MARK_MIN_US, BASE_IR_DATA_MARK_MAX_US))
      {
        accept_frame(dir);
      }
      s_rx_state[dir] = RX_WAIT_LEADER;
      break;

    case RX_WAIT_LEADER:
    default:
      break;
  }
}

void BaseIr_Task(void)
{
  uint32_t now;
  uint16_t frames[BASE_IR_COUNT];
  uint32_t primask;
  int candidate;
  uint16_t candidate_score;
  int i;

  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    g_base_ir_level[i] = (HAL_GPIO_ReadPin(s_ch[i].port, s_ch[i].pin) == GPIO_PIN_SET) ? 1u : 0u;
  }

  now = HAL_GetTick();
  if ((now - s_win_start) < BASE_IR_SCORE_WINDOW_MS)
  {
    return;
  }

  /* Snapshot decoded-frame counters atomically. */
  primask = __get_PRIMASK();
  __disable_irq();
  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    frames[i] = s_valid_frames[i];
    s_valid_frames[i] = 0u;
  }
  if (primask == 0u) __enable_irq();

  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    uint16_t raw = (frames[i] >= 2u) ? 1000u : (uint16_t)(frames[i] * 500u);

    /* Four-sample IIR suppresses the HA percentage jumps without delaying the
     * strictly decoded presence flag. */
    g_base_ir_activity[i] =
        (uint16_t)(((uint32_t)g_base_ir_activity[i] * 3u + raw + 2u) / 4u);

    if (frames[i] != 0u)
    {
      uint16_t confirmed = (uint16_t)s_presence_frames[i] + frames[i];
      s_presence_frames[i] = (confirmed >= BASE_IR_ASSERT_FRAMES) ?
                             BASE_IR_ASSERT_FRAMES : (uint8_t)confirmed;
    }

    if (s_last_frame_ms[i] != 0u &&
        (now - s_last_frame_ms[i]) <= BASE_IR_RELEASE_MS)
    {
      g_base_ir_seen[i] = (s_presence_frames[i] >= BASE_IR_ASSERT_FRAMES) ? 1u : 0u;
    }
    else
    {
      g_base_ir_seen[i] = 0u;
      s_presence_frames[i] = 0u;
      g_base_ir_activity[i] = 0u;
    }
  }

  candidate = -1;
  candidate_score = 0u;
  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    if (g_base_ir_seen[i] &&
        (candidate < 0 || g_base_ir_activity[i] > candidate_score))
    {
      candidate = i;
      candidate_score = g_base_ir_activity[i];
    }
  }

  if (candidate < 0)
  {
    s_direction = -1;
    s_direction_candidate = -1;
    s_direction_windows = 0u;
  }
  else if (s_direction < 0 || !g_base_ir_seen[s_direction])
  {
    s_direction = (int8_t)candidate;
    s_direction_candidate = -1;
    s_direction_windows = 0u;
  }
  else if (candidate == s_direction ||
           candidate_score < (uint16_t)(g_base_ir_activity[s_direction] +
                                         BASE_IR_DIRECTION_MARGIN))
  {
    s_direction_candidate = -1;
    s_direction_windows = 0u;
  }
  else
  {
    if (s_direction_candidate != candidate)
    {
      s_direction_candidate = (int8_t)candidate;
      s_direction_windows = 1u;
    }
    else if (s_direction_windows < BASE_IR_DIRECTION_WINDOWS)
    {
      s_direction_windows++;
    }

    if (s_direction_windows >= BASE_IR_DIRECTION_WINDOWS)
    {
      s_direction = (int8_t)candidate;
      s_direction_candidate = -1;
      s_direction_windows = 0u;
    }
  }
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
  return s_direction;
}
