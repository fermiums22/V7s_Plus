/**
  ******************************************************************************
  * @file    docking.c
  * @brief   Slow local homing controller using the five dock IR receivers.
  *
  * The controller never moves while disabled, after a bumper hit, on a motor
  * fault, or without periodic Modbus supervision. Dock contact is accepted only
  * after the measured dock rail stays above threshold for 500 ms.
  ******************************************************************************
  */
#include "docking.h"

#include "base_ir.h"
#include "bumper_hit.h"
#include "front_ir_bumper.h"
#include "modbus_slave.h"
#include "motor_control.h"

#include "main.h"

#define DOCKING_TASK_MS               50u
#define DOCKING_MAX_RUN_MS        120000u
#define DOCKING_CONTACT_MV         12000u
#define DOCKING_CONTACT_STABLE_MS    500u
#define DOCKING_SIGNAL_HOLD_MS        400u
/* A hand remote/EMI burst is much shorter than a real dock beacon. Do not
 * leave the safe search turn for a direction until it remains qualified. */
#define DOCKING_BEACON_CONFIRM_MS     200u
#define DOCKING_SEARCH_RPM            25.0f
#define DOCKING_TURN_RPM              25.0f
#define DOCKING_APPROACH_RPM          30.0f
#define DOCKING_MIN_FORWARD_RPM       22.0f
#define DOCKING_MAX_FORWARD_RPM       38.0f

static DockingState s_state;
static uint8_t s_active;
static uint32_t s_started_ms;
static uint32_t s_last_task_ms;
static uint32_t s_last_signal_ms;
static uint32_t s_beacon_since_ms;
static uint32_t s_contact_since_ms;

static void stop_wheels(void)
{
  MotorControl_Stop();
}

static void set_wheels(float left_rpm, float right_rpm)
{
  MotorControl_SetWheelSpeed(left_rpm);
  MotorControl_SetRightWheelSpeed(right_rpm);
}

static uint8_t motor_faulted(void)
{
  MotorStatus left;
  MotorStatus right;
  MotorControl_GetMotorStatus(MOTOR_LEFT, &left);
  MotorControl_GetMotorStatus(MOTOR_RIGHT, &right);
  return (left.fault_active || left.fault_latched ||
          right.fault_active || right.fault_latched ||
          left.state == MOTOR_STATE_STALLED ||
          right.state == MOTOR_STATE_STALLED) ? 1u : 0u;
}

static float clamp_rpm(float value)
{
  if (value < DOCKING_MIN_FORWARD_RPM) return DOCKING_MIN_FORWARD_RPM;
  if (value > DOCKING_MAX_FORWARD_RPM) return DOCKING_MAX_FORWARD_RPM;
  return value;
}

static void finish(DockingState state)
{
  stop_wheels();
  s_active = 0u;
  s_state = state;
}

void Docking_Init(void)
{
  s_state = DOCKING_IDLE;
  s_active = 0u;
  s_started_ms = HAL_GetTick();
  s_last_task_ms = s_started_ms;
  s_last_signal_ms = 0u;
  s_beacon_since_ms = 0u;
  s_contact_since_ms = 0u;
}

uint8_t Docking_Start(void)
{
  uint32_t now = HAL_GetTick();

  if (BumperHit_Active() || motor_faulted())
  {
    s_state = BumperHit_Active() ? DOCKING_FAILED_BUMPER : DOCKING_FAILED_MOTOR;
    return 0u;
  }
  if (FrontIrBumper_VinRailMilliVolts() >= DOCKING_CONTACT_MV)
  {
    finish(DOCKING_DOCKED);
    return 1u;
  }

  s_started_ms = now;
  s_last_task_ms = now - DOCKING_TASK_MS;
  s_last_signal_ms = 0u;
  s_beacon_since_ms = 0u;
  s_contact_since_ms = 0u;
  s_state = DOCKING_SEARCH;
  s_active = 1u;
  return 1u;
}

void Docking_Stop(void)
{
  if (s_active)
  {
    finish(DOCKING_STOPPED);
  }
  else
  {
    stop_wheels();
  }
}

void Docking_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t mask = 0u;
  int i;

  if (!s_active) return;
  if ((now - s_last_task_ms) < DOCKING_TASK_MS) return;
  s_last_task_ms = now;

  if (!ModbusSlave_MotorEnabled())
  {
    finish(DOCKING_STOPPED);
    return;
  }
  if (ModbusSlave_EmergencyStop())
  {
    finish(DOCKING_STOPPED);
    return;
  }
  if (BumperHit_Active())
  {
    finish(DOCKING_FAILED_BUMPER);
    return;
  }
  if (motor_faulted())
  {
    finish(DOCKING_FAILED_MOTOR);
    return;
  }
  if ((now - s_started_ms) >= DOCKING_MAX_RUN_MS)
  {
    finish(DOCKING_FAILED_TIMEOUT);
    return;
  }

  if (FrontIrBumper_VinRailMilliVolts() >= DOCKING_CONTACT_MV)
  {
    if (s_contact_since_ms == 0u) s_contact_since_ms = now;
    stop_wheels();
    if ((now - s_contact_since_ms) >= DOCKING_CONTACT_STABLE_MS)
    {
      finish(DOCKING_DOCKED);
    }
    return;
  }
  s_contact_since_ms = 0u;

  for (i = 0; i < BASE_IR_COUNT; i++)
  {
    if (g_base_ir_seen[i]) mask |= (uint8_t)(1u << i);
  }
  if (mask == 0u)
  {
    s_beacon_since_ms = 0u;
  }
  else
  {
    if (s_beacon_since_ms == 0u) s_beacon_since_ms = now;
    if ((now - s_beacon_since_ms) < DOCKING_BEACON_CONFIRM_MS)
    {
      s_state = DOCKING_SEARCH;
      set_wheels(-DOCKING_SEARCH_RPM, DOCKING_SEARCH_RPM);
      return;
    }
    s_last_signal_ms = now;
  }

  if ((mask & ((1u << BASE_IR_FRONT_L) | (1u << BASE_IR_FRONT_R))) != 0u)
  {
    float correction = ((float)g_base_ir_activity[BASE_IR_FRONT_L] -
                        (float)g_base_ir_activity[BASE_IR_FRONT_R]) * 0.012f;
    float left = clamp_rpm(DOCKING_APPROACH_RPM - correction);
    float right = clamp_rpm(DOCKING_APPROACH_RPM + correction);
    s_state = DOCKING_APPROACH;
    set_wheels(left, right);
  }
  else if ((mask & (1u << BASE_IR_LEFT)) != 0u)
  {
    s_state = DOCKING_TURN_LEFT;
    set_wheels(-DOCKING_TURN_RPM, DOCKING_TURN_RPM);
  }
  else if ((mask & (1u << BASE_IR_RIGHT)) != 0u)
  {
    s_state = DOCKING_TURN_RIGHT;
    set_wheels(DOCKING_TURN_RPM, -DOCKING_TURN_RPM);
  }
  else if ((mask & (1u << BASE_IR_REAR)) != 0u)
  {
    s_state = DOCKING_TURN_LEFT;
    set_wheels(-DOCKING_TURN_RPM, DOCKING_TURN_RPM);
  }
  else if (s_last_signal_ms != 0u && (now - s_last_signal_ms) < DOCKING_SIGNAL_HOLD_MS)
  {
    stop_wheels();
  }
  else
  {
    s_state = DOCKING_SEARCH;
    set_wheels(-DOCKING_SEARCH_RPM, DOCKING_SEARCH_RPM);
  }
}

uint8_t Docking_Active(void)
{
  return s_active;
}

DockingState Docking_State(void)
{
  return s_state;
}

uint16_t Docking_ElapsedSeconds(void)
{
  if (s_state == DOCKING_IDLE) return 0u;
  return (uint16_t)((HAL_GetTick() - s_started_ms) / 1000u);
}

const char *Docking_StateName(DockingState state)
{
  switch (state)
  {
    case DOCKING_IDLE: return "idle";
    case DOCKING_SEARCH: return "search";
    case DOCKING_TURN_LEFT: return "turn_left";
    case DOCKING_TURN_RIGHT: return "turn_right";
    case DOCKING_APPROACH: return "approach";
    case DOCKING_DOCKED: return "docked";
    case DOCKING_STOPPED: return "stopped";
    case DOCKING_FAILED_TIMEOUT: return "timeout";
    case DOCKING_FAILED_BUMPER: return "bumper";
    case DOCKING_FAILED_MOTOR: return "motor_fault";
    default: return "unknown";
  }
}
