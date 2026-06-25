#include "motor_control.h"
#include "bumper_hit.h"
#include "front_ir_bumper.h"   /* VPROPI motor-current sense (PB1/ch9, PC5/ch15) */

#define MOTOR_L_PWM_TIMER_CHANNEL TIM_CHANNEL_3
#define MOTOR_R_PWM_TIMER_CHANNEL TIM_CHANNEL_1
#define MOTOR_PWM_PERIOD_COUNTS 2399
#define MOTOR_PWM_MIN_COUNTS 320
#define MOTOR_CONTROL_PERIOD_MS 20
#define MOTOR_POSITION_TOLERANCE_EDGES 10
#define MOTOR_POSITION_KP 4
#define MOTOR_SPEED_KP_NUM 1
#define MOTOR_SPEED_KP_DEN 2
#define MOTOR_SPEED_KI_DEN 32
#define MOTOR_SPEED_INTEGRAL_LIMIT 24000
#define MOTOR_PI 3.14159265f
#define MOTOR_L_ENCODER_EDGES_PER_WHEEL_REV 1130.0f
#define MOTOR_WHEEL_DIAMETER_M 0.070f
#define MOTOR_WHEEL_CIRCUMFERENCE_M (MOTOR_WHEEL_DIAMETER_M * MOTOR_PI)
#define MOTOR_WHEEL_BASE_M 0.240f
#define MOTOR_SECONDS_PER_MINUTE 60.0f
/* nFAULT (DRV8801, open-drain, idle high) latch: a real OCP/short on the driver
 * auto-retries every tOCP=1.2 ms, so nFAULT PULSES rather than sitting low. Hold
 * the "active" verdict for a few ms to bridge those gaps and not flicker. */
#define MOTOR_FAULT_HOLD_MS 8
/* Stall: commanded with drive applied but wheel not turning this long. */
#define MOTOR_MOVING_EDGES_PER_S 15
#define MOTOR_STALL_MS 200

typedef struct
{
  TIM_HandleTypeDef *pwm_timer;
  ADC_HandleTypeDef *current_adc;
  MotorControlMode mode;
  int32_t target_position_edges;
  int32_t target_speed_edges_per_s;
  int32_t max_position_speed_edges_per_s;
  int32_t speed_integral;
  int32_t last_position_edges;
  MotorControlMode right_mode;
  int32_t right_target_position_edges;
  int32_t right_target_speed_edges_per_s;
  int32_t right_max_position_speed_edges_per_s;
  int32_t right_speed_integral;
  int32_t right_last_position_edges;
  uint32_t last_control_tick_ms;
  int8_t commanded_direction;
  int8_t right_commanded_direction;
  uint8_t initialized;
} MotorControlState;

volatile int32_t g_motor_l_position_edges = 0;
volatile int32_t g_motor_l_speed_edges_per_s = 0;
volatile uint32_t g_motor_l_enc_edges = 0;
volatile uint8_t g_motor_l_enc_level = 0;
volatile int16_t g_motor_l_pwm_command = 0;
volatile int32_t g_motor_r_position_edges = 0;
volatile int32_t g_motor_r_speed_edges_per_s = 0;
volatile uint32_t g_motor_r_enc_edges = 0;
volatile uint8_t g_motor_r_enc_level = 0;
volatile int16_t g_motor_r_pwm_command = 0;

static MotorControlState s_motor;

/* Per-wheel driver-fault tracking (index by MotorSide). Sampled every task call
 * - off the 20 ms control gate - so the brief OCP-retry pulses are never missed. */
typedef struct
{
  uint8_t  latched;    /* sticky since boot / last ClearFault       */
  uint8_t  raw_prev;   /* last raw level, for high->low edge count   */
  uint8_t  ever_low;   /* a low was observed at least once           */
  uint32_t low_tick;   /* HAL tick of the most recent low sample     */
  uint32_t count;      /* number of high->low fault edges            */
} MotorFault;

static MotorFault s_fault[2];
static uint32_t   s_moving_tick[2];   /* last tick the wheel was turning / idle */

static GPIO_TypeDef *fault_port(MotorSide side)
{
  return (side == MOTOR_LEFT) ? MOTOR_L_NFAULT_GPIO_Port : MOTOR_R_NFAULT_GPIO_Port;
}

static uint16_t fault_pin(MotorSide side)
{
  return (side == MOTOR_LEFT) ? MOTOR_L_NFAULT_Pin : MOTOR_R_NFAULT_Pin;
}

static void fault_sample(void)
{
  uint32_t now = HAL_GetTick();
  int s;

  for (s = 0; s < 2; s++)
  {
    uint8_t raw = (HAL_GPIO_ReadPin(fault_port((MotorSide)s), fault_pin((MotorSide)s)) == GPIO_PIN_RESET) ? 1u : 0u;
    if (raw)
    {
      s_fault[s].low_tick = now;
      s_fault[s].ever_low = 1u;
      s_fault[s].latched  = 1u;
      if (!s_fault[s].raw_prev)
      {
        s_fault[s].count++;   /* count distinct fault edges (OCP-retry storms show up here) */
      }
    }
    s_fault[s].raw_prev = raw;
  }
}

static int fault_active(MotorSide side)
{
  return (s_fault[side].ever_low &&
          (HAL_GetTick() - s_fault[side].low_tick) < MOTOR_FAULT_HOLD_MS) ? 1 : 0;
}

static MotorState motor_state(MotorSide side)
{
  MotorControlMode mode = (side == MOTOR_LEFT) ? s_motor.mode : s_motor.right_mode;
  int16_t pwm           = (side == MOTOR_LEFT) ? g_motor_l_pwm_command : g_motor_r_pwm_command;

  if (fault_active(side))
  {
    return MOTOR_STATE_FAULT;
  }
  if (mode == MOTOR_CONTROL_MODE_OFF)
  {
    return MOTOR_STATE_IDLE;
  }
  if (pwm != 0 && (HAL_GetTick() - s_moving_tick[side]) > MOTOR_STALL_MS)
  {
    return MOTOR_STATE_STALLED;
  }
  return MOTOR_STATE_RUNNING;
}

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
  if (value < min_value)
  {
    return min_value;
  }
  if (value > max_value)
  {
    return max_value;
  }
  return value;
}

static int32_t abs_i32(int32_t value)
{
  return (value < 0) ? -value : value;
}

static float edges_to_rev(int32_t edges)
{
  return (float)edges / MOTOR_L_ENCODER_EDGES_PER_WHEEL_REV;
}

static int32_t rev_to_edges(float rev)
{
  float edges = rev * MOTOR_L_ENCODER_EDGES_PER_WHEEL_REV;

  return (int32_t)((edges >= 0.0f) ? (edges + 0.5f) : (edges - 0.5f));
}

static float edges_per_s_to_rpm(int32_t edges_per_s)
{
  return ((float)edges_per_s * MOTOR_SECONDS_PER_MINUTE) / MOTOR_L_ENCODER_EDGES_PER_WHEEL_REV;
}

static int32_t rpm_to_edges_per_s(float rpm)
{
  float edges_per_s_f = (rpm * MOTOR_L_ENCODER_EDGES_PER_WHEEL_REV) / MOTOR_SECONDS_PER_MINUTE;
  int32_t edges_per_s = (int32_t)((edges_per_s_f >= 0.0f) ? (edges_per_s_f + 0.5f) : (edges_per_s_f - 0.5f));

  if (rpm != 0.0f && edges_per_s == 0)
  {
    edges_per_s = (rpm > 0.0f) ? 1 : -1;
  }

  return edges_per_s;
}

static float rev_to_m(float rev)
{
  return rev * MOTOR_WHEEL_CIRCUMFERENCE_M;
}

static float m_to_rev(float meters)
{
  return meters / MOTOR_WHEEL_CIRCUMFERENCE_M;
}

static float rpm_to_mps(float rpm)
{
  return (rpm * MOTOR_WHEEL_CIRCUMFERENCE_M) / MOTOR_SECONDS_PER_MINUTE;
}

static float mps_to_rpm(float mps)
{
  return (mps * MOTOR_SECONDS_PER_MINUTE) / MOTOR_WHEEL_CIRCUMFERENCE_M;
}

static void sensor_power_set(uint8_t enabled)
{
  HAL_GPIO_WritePin(SWITCHED_SENSOR_5V_EN_GPIO_Port, SWITCHED_SENSOR_5V_EN_Pin,
                    enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void motor_apply_pwm(int16_t signed_pwm)
{
  int32_t duty = signed_pwm;

  if (duty == 0)
  {
    __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_L_PWM_TIMER_CHANNEL, 0);
    g_motor_l_pwm_command = 0;
    return;
  }

  if (duty > 0)
  {
    HAL_GPIO_WritePin(MOTOR_L_PHASE_GPIO_Port, MOTOR_L_PHASE_Pin, GPIO_PIN_SET);
    s_motor.commanded_direction = 1;
  }
  else
  {
    HAL_GPIO_WritePin(MOTOR_L_PHASE_GPIO_Port, MOTOR_L_PHASE_Pin, GPIO_PIN_RESET);
    s_motor.commanded_direction = -1;
    duty = -duty;
  }

  duty = clamp_i32(duty, MOTOR_PWM_MIN_COUNTS, MOTOR_PWM_PERIOD_COUNTS);
  __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_L_PWM_TIMER_CHANNEL, (uint32_t)duty);
  g_motor_l_pwm_command = (int16_t)((s_motor.commanded_direction > 0) ? duty : -duty);
}

static void right_motor_apply_pwm(int16_t signed_pwm)
{
  int32_t duty = signed_pwm;

  if (duty == 0)
  {
    __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_R_PWM_TIMER_CHANNEL, 0);
    g_motor_r_pwm_command = 0;
    return;
  }

  if (duty > 0)
  {
    HAL_GPIO_WritePin(MOTOR_R_PHASE_GPIO_Port, MOTOR_R_PHASE_Pin, GPIO_PIN_RESET);
    s_motor.right_commanded_direction = 1;
  }
  else
  {
    HAL_GPIO_WritePin(MOTOR_R_PHASE_GPIO_Port, MOTOR_R_PHASE_Pin, GPIO_PIN_SET);
    s_motor.right_commanded_direction = -1;
    duty = -duty;
  }

  duty = clamp_i32(duty, MOTOR_PWM_MIN_COUNTS, MOTOR_PWM_PERIOD_COUNTS);
  __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_R_PWM_TIMER_CHANNEL, (uint32_t)duty);
  g_motor_r_pwm_command = (int16_t)((s_motor.right_commanded_direction > 0) ? duty : -duty);
}

/* Encoder edge from the shared HAL_GPIO_EXTI_Callback dispatcher (main.c).
 * Single-channel reflective encoders: every rising+falling transition is one
 * edge; direction comes from the last commanded sign (no quadrature). Runs in
 * EXTI ISR context, so it never misses an edge the way the old 50 Hz poll did. */
void MotorControl_OnEncoderEdge(uint16_t pin)
{
  if (pin == MOTOR_L_ENC_SIGNAL_Pin)
  {
    g_motor_l_enc_level = (uint8_t)HAL_GPIO_ReadPin(MOTOR_L_ENC_SIGNAL_GPIO_Port, MOTOR_L_ENC_SIGNAL_Pin);
    g_motor_l_enc_edges++;
    g_motor_l_position_edges += s_motor.commanded_direction;
  }
  else if (pin == MOTOR_R_ENC_SIGNAL_Pin)
  {
    g_motor_r_enc_level = (uint8_t)HAL_GPIO_ReadPin(MOTOR_R_ENC_SIGNAL_GPIO_Port, MOTOR_R_ENC_SIGNAL_Pin);
    g_motor_r_enc_edges++;
    g_motor_r_position_edges += s_motor.right_commanded_direction;
  }
}

static void update_speed_estimate(uint32_t now_ms)
{
  uint32_t elapsed_ms = now_ms - s_motor.last_control_tick_ms;
  int32_t position_now = g_motor_l_position_edges;
  int32_t right_position_now = g_motor_r_position_edges;
  int32_t delta_edges;
  int32_t right_delta_edges;

  if (elapsed_ms < MOTOR_CONTROL_PERIOD_MS)
  {
    return;
  }

  delta_edges = position_now - s_motor.last_position_edges;
  right_delta_edges = right_position_now - s_motor.right_last_position_edges;
  g_motor_l_speed_edges_per_s = (delta_edges * 1000) / (int32_t)elapsed_ms;
  g_motor_r_speed_edges_per_s = (right_delta_edges * 1000) / (int32_t)elapsed_ms;
  s_motor.last_position_edges = position_now;
  s_motor.right_last_position_edges = right_position_now;
  s_motor.last_control_tick_ms = now_ms;

  /* Refresh the stall timer while a wheel is idle or actually turning; it only
     ages (toward STALL) once a wheel is commanded yet sitting still. */
  if (s_motor.mode == MOTOR_CONTROL_MODE_OFF ||
      abs_i32(g_motor_l_speed_edges_per_s) >= MOTOR_MOVING_EDGES_PER_S)
  {
    s_moving_tick[MOTOR_LEFT] = now_ms;
  }
  if (s_motor.right_mode == MOTOR_CONTROL_MODE_OFF ||
      abs_i32(g_motor_r_speed_edges_per_s) >= MOTOR_MOVING_EDGES_PER_S)
  {
    s_moving_tick[MOTOR_RIGHT] = now_ms;
  }
}

static int32_t position_loop_target_speed(void)
{
  int32_t error = s_motor.target_position_edges - g_motor_l_position_edges;
  int32_t target_speed;
  int32_t max_speed = abs_i32(s_motor.max_position_speed_edges_per_s);

  if (abs_i32(error) <= MOTOR_POSITION_TOLERANCE_EDGES)
  {
    return 0;
  }

  target_speed = error * MOTOR_POSITION_KP;
  return clamp_i32(target_speed, -max_speed, max_speed);
}

static int32_t right_position_loop_target_speed(void)
{
  int32_t error = s_motor.right_target_position_edges - g_motor_r_position_edges;
  int32_t target_speed;
  int32_t max_speed = abs_i32(s_motor.right_max_position_speed_edges_per_s);

  if (abs_i32(error) <= MOTOR_POSITION_TOLERANCE_EDGES)
  {
    return 0;
  }

  target_speed = error * MOTOR_POSITION_KP;
  return clamp_i32(target_speed, -max_speed, max_speed);
}

static void speed_loop_apply(int32_t target_speed_edges_per_s)
{
  int32_t error = target_speed_edges_per_s - g_motor_l_speed_edges_per_s;
  int32_t output;

  if (target_speed_edges_per_s == 0)
  {
    s_motor.speed_integral = 0;
    motor_apply_pwm(0);
    return;
  }

  s_motor.speed_integral = clamp_i32(s_motor.speed_integral + error,
                                     -MOTOR_SPEED_INTEGRAL_LIMIT,
                                     MOTOR_SPEED_INTEGRAL_LIMIT);
  output = ((error * MOTOR_SPEED_KP_NUM) / MOTOR_SPEED_KP_DEN) +
           (s_motor.speed_integral / MOTOR_SPEED_KI_DEN);
  output = clamp_i32(output, -MOTOR_PWM_PERIOD_COUNTS, MOTOR_PWM_PERIOD_COUNTS);
  motor_apply_pwm((int16_t)output);
}

static void right_speed_loop_apply(int32_t target_speed_edges_per_s)
{
  int32_t error = target_speed_edges_per_s - g_motor_r_speed_edges_per_s;
  int32_t output;

  if (target_speed_edges_per_s == 0)
  {
    s_motor.right_speed_integral = 0;
    right_motor_apply_pwm(0);
    return;
  }

  s_motor.right_speed_integral = clamp_i32(s_motor.right_speed_integral + error,
                                           -MOTOR_SPEED_INTEGRAL_LIMIT,
                                           MOTOR_SPEED_INTEGRAL_LIMIT);
  output = ((error * MOTOR_SPEED_KP_NUM) / MOTOR_SPEED_KP_DEN) +
           (s_motor.right_speed_integral / MOTOR_SPEED_KI_DEN);
  output = clamp_i32(output, -MOTOR_PWM_PERIOD_COUNTS, MOTOR_PWM_PERIOD_COUNTS);
  right_motor_apply_pwm((int16_t)output);
}

void MotorControl_Init(TIM_HandleTypeDef *pwm_timer, ADC_HandleTypeDef *current_adc)
{
  s_motor.pwm_timer = pwm_timer;
  s_motor.current_adc = current_adc;
  s_motor.mode = MOTOR_CONTROL_MODE_OFF;
  s_motor.target_position_edges = 0;
  s_motor.target_speed_edges_per_s = 0;
  s_motor.max_position_speed_edges_per_s = 0;
  s_motor.speed_integral = 0;
  s_motor.last_position_edges = 0;
  s_motor.right_mode = MOTOR_CONTROL_MODE_OFF;
  s_motor.right_target_position_edges = 0;
  s_motor.right_target_speed_edges_per_s = 0;
  s_motor.right_max_position_speed_edges_per_s = 0;
  s_motor.right_speed_integral = 0;
  s_motor.right_last_position_edges = 0;
  s_motor.last_control_tick_ms = HAL_GetTick();
  s_motor.commanded_direction = -1;
  s_motor.right_commanded_direction = -1;
  s_motor.initialized = 1;

  s_fault[MOTOR_LEFT]  = (MotorFault){0};
  s_fault[MOTOR_RIGHT] = (MotorFault){0};
  s_moving_tick[MOTOR_LEFT]  = s_motor.last_control_tick_ms;
  s_moving_tick[MOTOR_RIGHT] = s_motor.last_control_tick_ms;

  sensor_power_set(1);
  HAL_GPIO_WritePin(Q24_GPIO_Port, Q24_Pin, GPIO_PIN_RESET);
  g_motor_l_enc_level = (uint8_t)HAL_GPIO_ReadPin(MOTOR_L_ENC_SIGNAL_GPIO_Port, MOTOR_L_ENC_SIGNAL_Pin);
  g_motor_r_enc_level = (uint8_t)HAL_GPIO_ReadPin(MOTOR_R_ENC_SIGNAL_GPIO_Port, MOTOR_R_ENC_SIGNAL_Pin);

  __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_L_PWM_TIMER_CHANNEL, 0);
  __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_R_PWM_TIMER_CHANNEL, 0);
  HAL_TIM_PWM_Start(s_motor.pwm_timer, MOTOR_L_PWM_TIMER_CHANNEL);
  HAL_TIM_PWM_Start(s_motor.pwm_timer, MOTOR_R_PWM_TIMER_CHANNEL);
}

void MotorControl_Task(void)
{
  uint32_t now_ms;
  int32_t target_speed = 0;
  int32_t right_target_speed = 0;

  if (!s_motor.initialized)
  {
    return;
  }

  /* Driver-fault sampling runs every call (sub-ms), off the control gate, so the
     pulsing nFAULT of an OCP auto-retry is never missed. While a fault is held,
     keep the wheels braked - the driver already disabled its bridge; we just stop
     commanding. The latch stays sticky until MotorControl_ClearFault(). */
  fault_sample();
  if (fault_active(MOTOR_LEFT) || fault_active(MOTOR_RIGHT))
  {
    MotorControl_Stop();
    return;
  }

  now_ms = HAL_GetTick();
  if ((now_ms - s_motor.last_control_tick_ms) < MOTOR_CONTROL_PERIOD_MS)
  {
    return;
  }

  update_speed_estimate(now_ms);

  /* Impact reflex: the bumper-hit EXTI ISR latches a status flag the instant the
     robot strikes something (or is kicked). We catch it here at the top of the
     control cycle and brake at once - no grinding into the wall. The latch stays
     set; the navigation layer decides how to back off / turn, then BumperHit_Clear. */
  if (BumperHit_Active())
  {
    MotorControl_Stop();
    return;
  }

  if (s_motor.mode == MOTOR_CONTROL_MODE_POSITION)
  {
    target_speed = position_loop_target_speed();
    s_motor.target_speed_edges_per_s = target_speed;
    if (target_speed == 0 && abs_i32(g_motor_l_speed_edges_per_s) < 20)
    {
      MotorControl_Stop();
      return;
    }
  }
  else if (s_motor.mode == MOTOR_CONTROL_MODE_SPEED)
  {
    target_speed = s_motor.target_speed_edges_per_s;
  }
  else
  {
    target_speed = 0;
  }

  speed_loop_apply(target_speed);

  if (s_motor.right_mode == MOTOR_CONTROL_MODE_POSITION)
  {
    right_target_speed = right_position_loop_target_speed();
    s_motor.right_target_speed_edges_per_s = right_target_speed;
    if (right_target_speed == 0 && abs_i32(g_motor_r_speed_edges_per_s) < 20)
    {
      s_motor.right_mode = MOTOR_CONTROL_MODE_OFF;
      s_motor.right_speed_integral = 0;
      right_motor_apply_pwm(0);
      return;
    }
  }
  else if (s_motor.right_mode == MOTOR_CONTROL_MODE_SPEED)
  {
    right_target_speed = s_motor.right_target_speed_edges_per_s;
  }
  else
  {
    right_target_speed = 0;
  }

  right_speed_loop_apply(right_target_speed);
}

void MotorControl_Stop(void)
{
  if (!s_motor.initialized)
  {
    return;
  }
  s_motor.target_speed_edges_per_s = 0;
  s_motor.speed_integral = 0;
  s_motor.right_target_speed_edges_per_s = 0;
  s_motor.right_speed_integral = 0;
  motor_apply_pwm(0);
  right_motor_apply_pwm(0);
  s_motor.mode = MOTOR_CONTROL_MODE_OFF;
  s_motor.right_mode = MOTOR_CONTROL_MODE_OFF;
}

void MotorControl_SetRightTestPwm(int16_t signed_pwm)
{
  if (!s_motor.initialized)
  {
    return;
  }

  right_motor_apply_pwm(signed_pwm);
}

uint8_t MotorControl_IsRightBusy(void)
{
  return (s_motor.right_mode != MOTOR_CONTROL_MODE_OFF) ? 1U : 0U;
}

void MotorControl_SetSpeed(int32_t speed_edges_per_s)
{
  s_motor.target_speed_edges_per_s = speed_edges_per_s;
  s_motor.mode = (speed_edges_per_s == 0) ? MOTOR_CONTROL_MODE_OFF : MOTOR_CONTROL_MODE_SPEED;
}

void MotorControl_SetWheelSpeed(float speed_rpm)
{
  MotorControl_SetSpeed(rpm_to_edges_per_s(speed_rpm));
}

void MotorControl_SetLinearSpeed(float speed_mps)
{
  MotorControl_SetWheelSpeed(mps_to_rpm(speed_mps));
}

void MotorControl_SetRightWheelSpeed(float speed_rpm)
{
  int32_t eps = rpm_to_edges_per_s(speed_rpm);
  s_motor.right_target_speed_edges_per_s = eps;
  s_motor.right_speed_integral = 0;
  s_motor.right_mode = (eps == 0) ? MOTOR_CONTROL_MODE_OFF : MOTOR_CONTROL_MODE_SPEED;
}

void MotorControl_MoveTo(int32_t target_position_edges, int32_t max_speed_edges_per_s)
{
  s_motor.target_position_edges = target_position_edges;
  s_motor.max_position_speed_edges_per_s = (max_speed_edges_per_s == 0) ? 500 : max_speed_edges_per_s;
  s_motor.speed_integral = 0;
  s_motor.mode = MOTOR_CONTROL_MODE_POSITION;
}

void MotorControl_MoveRelative(int32_t delta_edges, int32_t max_speed_edges_per_s)
{
  MotorControl_MoveTo(g_motor_l_position_edges + delta_edges, max_speed_edges_per_s);
}

void MotorControl_MoveWheelTo(float target_position_rev, float max_speed_rpm)
{
  MotorControl_MoveTo(rev_to_edges(target_position_rev), rpm_to_edges_per_s(max_speed_rpm));
}

void MotorControl_MoveWheelRelative(float delta_rev, float max_speed_rpm)
{
  MotorControl_MoveRelative(rev_to_edges(delta_rev), rpm_to_edges_per_s(max_speed_rpm));
}

void MotorControl_MoveRightWheelTo(float target_position_rev, float max_speed_rpm)
{
  s_motor.right_target_position_edges = rev_to_edges(target_position_rev);
  s_motor.right_max_position_speed_edges_per_s = rpm_to_edges_per_s(max_speed_rpm);
  if (s_motor.right_max_position_speed_edges_per_s == 0)
  {
    s_motor.right_max_position_speed_edges_per_s = 500;
  }
  s_motor.right_speed_integral = 0;
  s_motor.right_mode = MOTOR_CONTROL_MODE_POSITION;
}

void MotorControl_MoveRightWheelRelative(float delta_rev, float max_speed_rpm)
{
  MotorControl_MoveRightWheelTo(edges_to_rev(g_motor_r_position_edges) + delta_rev, max_speed_rpm);
}

void MotorControl_MoveLinearTo(float target_position_m, float max_speed_mps)
{
  MotorControl_MoveWheelTo(m_to_rev(target_position_m), mps_to_rpm(max_speed_mps));
}

void MotorControl_MoveLinearRelative(float delta_m, float max_speed_mps)
{
  MotorControl_MoveWheelRelative(m_to_rev(delta_m), mps_to_rpm(max_speed_mps));
}

float MotorControl_RobotTurnWheelDistanceByRadians(float angle_rad)
{
  return (angle_rad * MOTOR_WHEEL_BASE_M) * 0.5f;
}

float MotorControl_RobotTurnWheelDistanceByDegrees(float angle_deg)
{
  return MotorControl_RobotTurnWheelDistanceByRadians((angle_deg * MOTOR_PI) / 180.0f);
}

void MotorControl_GetStatus(MotorControlStatus *status)
{
  if (status == 0)
  {
    return;
  }

  status->position_edges = g_motor_l_position_edges;
  status->target_position_edges = s_motor.target_position_edges;
  status->speed_edges_per_s = g_motor_l_speed_edges_per_s;
  status->target_speed_edges_per_s = s_motor.target_speed_edges_per_s;
  status->position_rev = edges_to_rev(g_motor_l_position_edges);
  status->target_position_rev = edges_to_rev(s_motor.target_position_edges);
  status->speed_rpm = edges_per_s_to_rpm(g_motor_l_speed_edges_per_s);
  status->target_speed_rpm = edges_per_s_to_rpm(s_motor.target_speed_edges_per_s);
  status->position_m = rev_to_m(status->position_rev);
  status->target_position_m = rev_to_m(status->target_position_rev);
  status->speed_mps = rpm_to_mps(status->speed_rpm);
  status->target_speed_mps = rpm_to_mps(status->target_speed_rpm);
  status->pwm_command = g_motor_l_pwm_command;
  status->edge_count = g_motor_l_enc_edges;
  status->encoder_level = g_motor_l_enc_level;
  status->fault_active = (uint8_t)fault_active(MOTOR_LEFT);
  status->mode = s_motor.mode;
}

void MotorControl_GetMotorStatus(MotorSide side, MotorStatus *status)
{
  int left = (side == MOTOR_LEFT);

  if (status == 0 || (side != MOTOR_LEFT && side != MOTOR_RIGHT))
  {
    return;
  }

  status->state             = motor_state(side);
  status->mode              = left ? s_motor.mode : s_motor.right_mode;
  status->position_edges    = left ? g_motor_l_position_edges : g_motor_r_position_edges;
  status->position_rev      = edges_to_rev(status->position_edges);
  status->speed_edges_per_s = left ? g_motor_l_speed_edges_per_s : g_motor_r_speed_edges_per_s;
  status->speed_rpm         = edges_per_s_to_rpm(status->speed_edges_per_s);
  status->pwm_command       = left ? g_motor_l_pwm_command : g_motor_r_pwm_command;
  status->current_adc       = left ? g_motor_l_isense_adc : g_motor_r_isense_adc;
  status->current_mv        = left ? FrontIrBumper_MotorLMilliVolts() : FrontIrBumper_MotorRMilliVolts();
  status->edge_count        = left ? g_motor_l_enc_edges : g_motor_r_enc_edges;
  status->encoder_level     = left ? g_motor_l_enc_level : g_motor_r_enc_level;
  status->fault_active      = (uint8_t)fault_active(side);
  status->fault_latched     = s_fault[side].latched;
  status->fault_count       = s_fault[side].count;
}

const char *MotorControl_StateName(MotorState state)
{
  switch (state)
  {
    case MOTOR_STATE_IDLE:    return "IDLE";
    case MOTOR_STATE_RUNNING: return "RUN";
    case MOTOR_STATE_STALLED: return "STALL";
    case MOTOR_STATE_FAULT:   return "FAULT";
    default:                  return "?";
  }
}

void MotorControl_ClearFault(void)
{
  s_fault[MOTOR_LEFT]  = (MotorFault){0};
  s_fault[MOTOR_RIGHT] = (MotorFault){0};
}
