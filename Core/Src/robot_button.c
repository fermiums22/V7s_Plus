/**
  ******************************************************************************
  * @file    robot_button.c
  * @brief   Debounced J12 touch button and two-colour PWM indicator. User-owned.
  *
  * PA0 is sampled after EXTI edges and debounced in the main loop. The idle
  * level is learned at boot because the exact TCH223 polarity is not yet
  * measured. PB15/TIM15_CH2 and PB9/TIM17_CH1 are configured by CubeMX from
  * V7s_Plus.ioc; this module only starts PWM and updates compare values.
  ******************************************************************************
  */
#include "robot_button.h"

#include "modbus_slave.h"

#define BUTTON_DEBOUNCE_MS              35u
#define BUTTON_IDLE_LEARN_MS           250u
#define BUTTON_DISABLED_YELLOW_PM      180u
#define BUTTON_ENABLED_ORANGE_PM       320u

static TIM_HandleTypeDef *s_yellow_tim;
static TIM_HandleTypeDef *s_orange_tim;
static uint32_t s_last_change_ms;
static uint32_t s_boot_ms;
static uint8_t s_candidate_level;
static uint8_t s_stable_level;
static uint8_t s_idle_level;
static uint8_t s_pressed;
static uint8_t s_ready;
static volatile uint8_t s_edge_pending;
static uint16_t s_yellow_permille;
static uint16_t s_orange_permille;

static uint8_t read_level(void)
{
  return (HAL_GPIO_ReadPin(ROBOT_TOUCH_BUTTON_GPIO_Port,
                          ROBOT_TOUCH_BUTTON_Pin) == GPIO_PIN_SET) ? 1u : 0u;
}

static uint16_t clamp_permille(uint16_t value)
{
  return (value > 1000u) ? 1000u : value;
}

static void set_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint16_t permille)
{
  uint32_t period;
  uint32_t compare;

  if (timer == 0) return;
  period = __HAL_TIM_GET_AUTORELOAD(timer) + 1u;
  compare = (period * clamp_permille(permille)) / 1000u;
  if (compare > __HAL_TIM_GET_AUTORELOAD(timer))
  {
    compare = __HAL_TIM_GET_AUTORELOAD(timer);
  }
  __HAL_TIM_SET_COMPARE(timer, channel, compare);
}

void RobotButton_Init(TIM_HandleTypeDef *yellow_pwm, TIM_HandleTypeDef *orange_pwm)
{
  uint8_t level = read_level();

  s_yellow_tim = yellow_pwm;
  s_orange_tim = orange_pwm;
  s_boot_ms = HAL_GetTick();
  s_last_change_ms = s_boot_ms;
  s_candidate_level = level;
  s_stable_level = level;
  s_idle_level = level;
  s_pressed = 0u;
  s_ready = 0u;
  s_edge_pending = 0u;
  s_yellow_permille = 0u;
  s_orange_permille = 0u;

  (void)HAL_TIM_PWM_Start(s_yellow_tim, TIM_CHANNEL_2);
  (void)HAL_TIM_PWM_Start(s_orange_tim, TIM_CHANNEL_1);
  RobotButton_SetEnabledIndicator(0u);
}

void RobotButton_OnEdge(void)
{
  s_edge_pending = 1u;
}

void RobotButton_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t level = read_level();

  if (!s_ready)
  {
    s_idle_level = level;
    s_candidate_level = level;
    s_stable_level = level;
    if ((now - s_boot_ms) >= BUTTON_IDLE_LEARN_MS)
    {
      s_ready = 1u;
    }
    return;
  }

  if (s_edge_pending || level != s_candidate_level)
  {
    s_edge_pending = 0u;
    if (level != s_candidate_level)
    {
      s_candidate_level = level;
      s_last_change_ms = now;
    }
  }

  if (s_candidate_level != s_stable_level &&
      (now - s_last_change_ms) >= BUTTON_DEBOUNCE_MS)
  {
    uint8_t was_pressed = s_pressed;
    s_stable_level = s_candidate_level;
    s_pressed = (s_stable_level != s_idle_level) ? 1u : 0u;
    if (s_pressed && !was_pressed)
    {
      ModbusSlave_SetMotorEnabled(ModbusSlave_MotorEnabled() ? 0u : 1u);
    }
  }
}

uint8_t RobotButton_RawLevel(void)
{
  return read_level();
}

uint8_t RobotButton_Pressed(void)
{
  return s_pressed;
}

uint16_t RobotButton_YellowPermille(void)
{
  return s_yellow_permille;
}

uint16_t RobotButton_OrangePermille(void)
{
  return s_orange_permille;
}

void RobotButton_SetEnabledIndicator(uint8_t enabled)
{
  if (enabled)
  {
    RobotButton_SetYellowPermille(0u);
    RobotButton_SetOrangePermille(BUTTON_ENABLED_ORANGE_PM);
  }
  else
  {
    RobotButton_SetYellowPermille(BUTTON_DISABLED_YELLOW_PM);
    RobotButton_SetOrangePermille(0u);
  }
}

void RobotButton_SetYellowPermille(uint16_t permille)
{
  s_yellow_permille = clamp_permille(permille);
  set_compare(s_yellow_tim, TIM_CHANNEL_2, s_yellow_permille);
}

void RobotButton_SetOrangePermille(uint16_t permille)
{
  s_orange_permille = clamp_permille(permille);
  set_compare(s_orange_tim, TIM_CHANNEL_1, s_orange_permille);
}
