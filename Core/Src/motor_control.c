#include "motor_control.h"

#define MOTOR_PWM_TIMER_CHANNEL TIM_CHANNEL_3
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
  uint32_t last_control_tick_ms;
  GPIO_PinState last_encoder_state;
  int8_t commanded_direction;
  uint8_t initialized;
} MotorControlState;

volatile int32_t g_motor_l_position_edges = 0;
volatile int32_t g_motor_l_speed_edges_per_s = 0;
volatile uint32_t g_motor_l_enc_edges = 0;
volatile uint8_t g_motor_l_enc_level = 0;
volatile int16_t g_motor_l_pwm_command = 0;

static MotorControlState s_motor;

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
  HAL_GPIO_WritePin(Q21_EN_ENC_VCC_GPIO_Port, Q21_EN_ENC_VCC_Pin,
                    enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void motor_apply_pwm(int16_t signed_pwm)
{
  int32_t duty = signed_pwm;

  if (duty == 0)
  {
    __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_PWM_TIMER_CHANNEL, 0);
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
  __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_PWM_TIMER_CHANNEL, (uint32_t)duty);
  g_motor_l_pwm_command = (int16_t)((s_motor.commanded_direction > 0) ? duty : -duty);
}

static void encoder_poll(void)
{
  GPIO_PinState enc_now = HAL_GPIO_ReadPin(MOTOR_L_ENC_SIGNAL_GPIO_Port, MOTOR_L_ENC_SIGNAL_Pin);
  g_motor_l_enc_level = (uint8_t)enc_now;

  if (enc_now != s_motor.last_encoder_state)
  {
    s_motor.last_encoder_state = enc_now;
    g_motor_l_enc_edges++;
    g_motor_l_position_edges += s_motor.commanded_direction;
  }
}

static void update_speed_estimate(uint32_t now_ms)
{
  uint32_t elapsed_ms = now_ms - s_motor.last_control_tick_ms;
  int32_t position_now = g_motor_l_position_edges;
  int32_t delta_edges;

  if (elapsed_ms < MOTOR_CONTROL_PERIOD_MS)
  {
    return;
  }

  delta_edges = position_now - s_motor.last_position_edges;
  g_motor_l_speed_edges_per_s = (delta_edges * 1000) / (int32_t)elapsed_ms;
  s_motor.last_position_edges = position_now;
  s_motor.last_control_tick_ms = now_ms;
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
  s_motor.last_control_tick_ms = HAL_GetTick();
  s_motor.commanded_direction = -1;
  s_motor.initialized = 1;

  sensor_power_set(1);
  HAL_GPIO_WritePin(Q24_GPIO_Port, Q24_Pin, GPIO_PIN_RESET);
  s_motor.last_encoder_state = HAL_GPIO_ReadPin(MOTOR_L_ENC_SIGNAL_GPIO_Port, MOTOR_L_ENC_SIGNAL_Pin);
  g_motor_l_enc_level = (uint8_t)s_motor.last_encoder_state;

  __HAL_TIM_SET_COMPARE(s_motor.pwm_timer, MOTOR_PWM_TIMER_CHANNEL, 0);
  HAL_TIM_PWM_Start(s_motor.pwm_timer, MOTOR_PWM_TIMER_CHANNEL);
}

void MotorControl_Task(void)
{
  uint32_t now_ms;
  int32_t target_speed = 0;

  if (!s_motor.initialized)
  {
    return;
  }

  encoder_poll();
  now_ms = HAL_GetTick();
  if ((now_ms - s_motor.last_control_tick_ms) < MOTOR_CONTROL_PERIOD_MS)
  {
    return;
  }

  update_speed_estimate(now_ms);

  if (HAL_GPIO_ReadPin(MOTOR_L_NFAULT_GPIO_Port, MOTOR_L_NFAULT_Pin) == GPIO_PIN_RESET)
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
}

void MotorControl_Stop(void)
{
  if (!s_motor.initialized)
  {
    return;
  }
  s_motor.target_speed_edges_per_s = 0;
  s_motor.speed_integral = 0;
  motor_apply_pwm(0);
  s_motor.mode = MOTOR_CONTROL_MODE_OFF;
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
  status->fault_active =
      (HAL_GPIO_ReadPin(MOTOR_L_NFAULT_GPIO_Port, MOTOR_L_NFAULT_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
  status->mode = s_motor.mode;
}
