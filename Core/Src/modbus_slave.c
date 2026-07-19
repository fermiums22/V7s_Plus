/**
  ******************************************************************************
  * @file    modbus_slave.c
  * @brief   Small Modbus RTU server for the ESP32-S3 application link.
  *
  * The UART is shared with the bring-up console. A Modbus request always starts
  * with slave address 1, while printable console commands do not, so binary
  * frames can be detected without removing the console.
  ******************************************************************************
  */
#include "modbus_slave.h"

#include "bumper_hit.h"
#include "base_ir.h"
#include "caster_odo.h"
#include "charge_control.h"
#include "cliff_ir.h"
#include "console.h"
#include "docking.h"
#include "front_ir_bumper.h"
#include "motor_control.h"
#include "robot_button.h"

#include "main.h"

#define MODBUS_ADDRESS             1u
#define MODBUS_FRAME_MAX           256u
#define MODBUS_FRAME_GAP_MS        3u
#define MODBUS_COMMAND_TIMEOUT_MS  5000u

#define FC_READ_COILS              0x01u
#define FC_READ_HOLDING_REGISTERS  0x03u
#define FC_READ_INPUT_REGISTERS    0x04u
#define FC_WRITE_SINGLE_COIL       0x05u
#define FC_WRITE_SINGLE_REGISTER   0x06u
#define FC_WRITE_MULTIPLE_COILS    0x0Fu
#define FC_WRITE_MULTIPLE_REGS     0x10u

static uint8_t s_rx[MODBUS_FRAME_MAX];
static uint16_t s_rx_size;
static uint16_t s_expected_size;
static uint32_t s_last_byte_ms;
static uint32_t s_last_request_ms;
static int16_t s_left_target_tenth_rpm;
static int16_t s_right_target_tenth_rpm;
static uint8_t s_motor_enabled;
static uint8_t s_emergency_stop;
static uint8_t s_ir_illumination = 1u;
static uint8_t s_link_seen;

static uint16_t crc16(const uint8_t *data, uint16_t size)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;

  for (i = 0; i < size; i++)
  {
    uint8_t bit;
    crc ^= data[i];
    for (bit = 0; bit < 8u; bit++)
    {
      crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
  }
  return crc;
}

static void send_frame(uint8_t *data, uint16_t size_without_crc)
{
  uint16_t crc = crc16(data, size_without_crc);
  data[size_without_crc] = (uint8_t)(crc & 0xFFu);
  data[size_without_crc + 1u] = (uint8_t)(crc >> 8);
  Console_WriteRaw(data, (uint16_t)(size_without_crc + 2u));
}

static void send_exception(uint8_t function, uint8_t exception)
{
  uint8_t response[5];
  response[0] = MODBUS_ADDRESS;
  response[1] = (uint8_t)(function | 0x80u);
  response[2] = exception;
  send_frame(response, 3u);
}

static int16_t rpm_to_tenths(float rpm)
{
  float scaled = rpm * 10.0f;
  if (scaled > 32767.0f) return 32767;
  if (scaled < -32768.0f) return -32768;
  return (int16_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static uint16_t high_word(int32_t value)
{
  return (uint16_t)(((uint32_t)value) >> 16);
}

static uint16_t low_word(int32_t value)
{
  return (uint16_t)((uint32_t)value & 0xFFFFu);
}

static uint16_t input_register(uint16_t address)
{
  MotorStatus left;
  MotorStatus right;
  uint32_t value32;

  MotorControl_GetMotorStatus(MOTOR_LEFT, &left);
  MotorControl_GetMotorStatus(MOTOR_RIGHT, &right);

  switch (address)
  {
    case 0x0000: return 0x0006u; /* application protocol version */
    case 0x0001: return (uint16_t)(HAL_GetTick() / 1000u);
    case 0x0004:
      return (uint16_t)((s_motor_enabled ? 0x0001u : 0u) |
                        (s_emergency_stop ? 0x0002u : 0u) |
                        (BumperHit_Active() ? 0x0004u : 0u) |
                        ((left.fault_active || left.fault_latched) ? 0x0008u : 0u) |
                        ((right.fault_active || right.fault_latched) ? 0x0010u : 0u) |
                        (RobotButton_Pressed() ? 0x0020u : 0u) |
                        (Docking_Active() ? 0x0040u : 0u) |
                        ((Docking_State() == DOCKING_DOCKED) ? 0x0080u : 0u));
    case 0x0010:
      value32 = FrontIrBumper_VbatPackMilliVolts();
      return (value32 > 65535u) ? 65535u : (uint16_t)value32;
    case 0x0011:
    {
      return (uint16_t)FrontIrBumper_BattMilliAmpsSigned();
    }
    case 0x0012:
      value32 = FrontIrBumper_VinRailMilliVolts();
      return (value32 > 65535u) ? 65535u : (uint16_t)value32;
    case 0x0020: return (uint16_t)g_front_ir_l_signal;
    case 0x0021: return (uint16_t)g_front_ir_f_signal;
    case 0x0022: return (uint16_t)g_front_ir_r_signal;
    case 0x0023: return (uint16_t)g_front_ir_l_rate;
    case 0x0024: return (uint16_t)g_front_ir_f_rate;
    case 0x0025: return (uint16_t)g_front_ir_r_rate;
    case 0x0030: return g_base_ir_activity[BASE_IR_FRONT_L];
    case 0x0031: return g_base_ir_activity[BASE_IR_FRONT_R];
    case 0x0032: return g_base_ir_activity[BASE_IR_LEFT];
    case 0x0033: return g_base_ir_activity[BASE_IR_RIGHT];
    case 0x0034: return g_base_ir_activity[BASE_IR_REAR];
    case 0x0035:
    {
      int direction = BaseIr_Direction();
      return (direction < 0) ? 0xFFFFu : (uint16_t)direction;
    }
    case 0x0036:
      return (uint16_t)((g_base_ir_seen[BASE_IR_FRONT_L] ? 0x01u : 0u) |
                        (g_base_ir_seen[BASE_IR_FRONT_R] ? 0x02u : 0u) |
                        (g_base_ir_seen[BASE_IR_LEFT] ? 0x04u : 0u) |
                        (g_base_ir_seen[BASE_IR_RIGHT] ? 0x08u : 0u) |
                        (g_base_ir_seen[BASE_IR_REAR] ? 0x10u : 0u));
    case 0x0037: return (uint16_t)Docking_State();
    case 0x0038: return Docking_ElapsedSeconds();
    case 0x0039:
      return (uint16_t)((RobotButton_RawLevel() ? 0x01u : 0u) |
                        (RobotButton_Pressed() ? 0x02u : 0u));
    case 0x003A: return RobotButton_YellowPermille();
    case 0x003B: return RobotButton_OrangePermille();
    case 0x003C:
    {
      const uint8_t events = BumperHit_Events();
      return (uint16_t)((BumperHit_Pressed(BUMPER_HIT_LEFT) ? 0x0001u : 0u) |
                        (BumperHit_Pressed(BUMPER_HIT_RIGHT) ? 0x0002u : 0u) |
                        ((events & (1u << BUMPER_HIT_LEFT)) ? 0x0100u : 0u) |
                        ((events & (1u << BUMPER_HIT_RIGHT)) ? 0x0200u : 0u));
    }
    case 0x0040: return (uint16_t)g_cliff_signal[CLIFF_FRONT_L];
    case 0x0041: return (uint16_t)g_cliff_signal[CLIFF_FRONT_R];
    case 0x0042: return (uint16_t)g_cliff_signal[CLIFF_RIGHT];
    case 0x0043: return (uint16_t)g_cliff_signal[CLIFF_LEFT];
    case 0x0044: return (uint16_t)g_cliff_signal[SIDE_IR];
    case 0x0048: return (uint16_t)g_cliff_rate[CLIFF_FRONT_L];
    case 0x0049: return (uint16_t)g_cliff_rate[CLIFF_FRONT_R];
    case 0x004A: return (uint16_t)g_cliff_rate[CLIFF_RIGHT];
    case 0x004B: return (uint16_t)g_cliff_rate[CLIFF_LEFT];
    case 0x004C: return (uint16_t)g_cliff_rate[SIDE_IR];
    case 0x0061: return (uint16_t)rpm_to_tenths(left.speed_rpm);
    case 0x0062: return high_word(left.position_edges);
    case 0x0063: return low_word(left.position_edges);
    case 0x0069: return (uint16_t)rpm_to_tenths(right.speed_rpm);
    case 0x006A: return high_word(right.position_edges);
    case 0x006B: return low_word(right.position_edges);
    case 0x0070:
      value32 = CasterOdo_Count();
      return (uint16_t)(value32 >> 16);
    case 0x0071:
      value32 = CasterOdo_Count();
      return (uint16_t)(value32 & 0xFFFFu);
    /* Charger diagnostic block: status, state, fault, duty, Vbat, signed
     * pack current (+ discharge / - charge), Vin and lifecycle sequence. */
    case 0x00B0: return ChargeControl_Status();
    case 0x00B1: return (uint16_t)ChargeControl_State();
    case 0x00B2: return (uint16_t)ChargeControl_Fault();
    case 0x00B3: return ChargeControl_DutyPermille();
    case 0x00B4: return ChargeControl_VbatMilliVolts();
    case 0x00B5: return (uint16_t)ChargeControl_BatteryCurrentMilliAmps();
    case 0x00B6: return ChargeControl_VinMilliVolts();
    case 0x00B7: return ChargeControl_Sequence();
    case 0x00B8: return ChargeControl_AutoEnabled() ? 1u : 0u;
    default: return 0u;
  }
}

static uint16_t holding_register(uint16_t address)
{
  switch (address)
  {
    case 0x0100: return (uint16_t)s_left_target_tenth_rpm;
    case 0x0101: return (uint16_t)s_right_target_tenth_rpm;
    case 0x0102: return RobotButton_YellowPermille();
    case 0x0103: return RobotButton_OrangePermille();
    default: return input_register(address);
  }
}

static uint8_t coil_value(uint16_t address)
{
  switch (address)
  {
    case 0: return s_motor_enabled;
    case 1: return s_emergency_stop;
    case 2: return 0u;
    case 3: return s_ir_illumination;
    case 4: return Docking_Active();
    case 7: return ChargeControl_Requested();
    case 8: return ChargeControl_AutoEnabled();
    default: return 0u;
  }
}

static void stop_motors(void)
{
  s_motor_enabled = 0u;
  s_left_target_tenth_rpm = 0;
  s_right_target_tenth_rpm = 0;
  Docking_Stop();
  MotorControl_Stop();
  RobotButton_SetEnabledIndicator(0u);
}

static void apply_motor_targets(void)
{
  MotorStatus left;
  MotorStatus right;

  MotorControl_GetMotorStatus(MOTOR_LEFT, &left);
  MotorControl_GetMotorStatus(MOTOR_RIGHT, &right);
  if (!s_motor_enabled || s_emergency_stop || BumperHit_Active() ||
      ChargeControl_Requested() ||
      left.fault_active || left.fault_latched || right.fault_active || right.fault_latched)
  {
    MotorControl_Stop();
    return;
  }

  MotorControl_SetWheelSpeed((float)s_left_target_tenth_rpm / 10.0f);
  MotorControl_SetRightWheelSpeed((float)s_right_target_tenth_rpm / 10.0f);
}

static uint8_t write_holding(uint16_t address, uint16_t value)
{
  int16_t signed_value = (int16_t)value;

  if (address == 0x0100)
  {
    if (signed_value > 1500) signed_value = 1500;
    if (signed_value < -1500) signed_value = -1500;
    s_left_target_tenth_rpm = signed_value;
    apply_motor_targets();
    return 1u;
  }
  if (address == 0x0101)
  {
    if (signed_value > 1500) signed_value = 1500;
    if (signed_value < -1500) signed_value = -1500;
    s_right_target_tenth_rpm = signed_value;
    apply_motor_targets();
    return 1u;
  }
  if (address == 0x0102)
  {
    RobotButton_SetYellowPermille(value);
    return 1u;
  }
  if (address == 0x0103)
  {
    RobotButton_SetOrangePermille(value);
    return 1u;
  }
  return 0u;
}

static uint8_t write_coil(uint16_t address, uint8_t enabled)
{
  switch (address)
  {
    case 0:
      ModbusSlave_SetMotorEnabled(enabled);
      return 1u;
    case 1:
      s_emergency_stop = enabled;
      if (enabled) stop_motors();
      return 1u;
    case 2:
      if (enabled)
      {
        BumperHit_Clear();
        MotorControl_ClearFault();
      }
      return 1u;
    case 3:
      s_ir_illumination = enabled;
      FrontIrBumper_SetIllumination(enabled ? 1 : 0);
      return 1u;
    case 4:
      if (enabled)
      {
        /* Driving to the dock and a commanded charger PWM are mutually
         * exclusive; never let docking re-enable motor outputs while charging. */
        if (ChargeControl_Requested()) return 1u;
        s_left_target_tenth_rpm = 0;
        s_right_target_tenth_rpm = 0;
        ModbusSlave_SetMotorEnabled(1u);
        if (!Docking_Start()) ModbusSlave_SetMotorEnabled(0u);
      }
      else
      {
        Docking_Stop();
      }
      return 1u;
    case 7:
      if (enabled)
      {
        stop_motors();
        ChargeControl_Start();
      }
      else
      {
        ChargeControl_Stop();
      }
      return 1u;
    case 8:
      ChargeControl_SetAutoEnabled(enabled);
      return 1u;
    default:
      return 0u;
  }
}

static uint16_t be16(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void handle_read_registers(uint8_t function)
{
  uint16_t start = be16(&s_rx[2]);
  uint16_t quantity = be16(&s_rx[4]);
  uint8_t response[MODBUS_FRAME_MAX];
  uint16_t i;

  if (quantity == 0u || quantity > 125u)
  {
    send_exception(function, 0x03u);
    return;
  }
  response[0] = MODBUS_ADDRESS;
  response[1] = function;
  response[2] = (uint8_t)(quantity * 2u);
  for (i = 0; i < quantity; i++)
  {
    uint16_t value = (function == FC_READ_HOLDING_REGISTERS) ?
                     holding_register((uint16_t)(start + i)) :
                     input_register((uint16_t)(start + i));
    response[3u + i * 2u] = (uint8_t)(value >> 8);
    response[4u + i * 2u] = (uint8_t)(value & 0xFFu);
  }
  send_frame(response, (uint16_t)(3u + quantity * 2u));
}

static void handle_read_coils(void)
{
  uint16_t start = be16(&s_rx[2]);
  uint16_t quantity = be16(&s_rx[4]);
  uint8_t response[32] = {0};
  uint16_t i;
  uint8_t byte_count;

  if (quantity == 0u || quantity > 64u)
  {
    send_exception(FC_READ_COILS, 0x03u);
    return;
  }
  byte_count = (uint8_t)((quantity + 7u) / 8u);
  response[0] = MODBUS_ADDRESS;
  response[1] = FC_READ_COILS;
  response[2] = byte_count;
  for (i = 0; i < quantity; i++)
  {
    if (coil_value((uint16_t)(start + i))) response[3u + i / 8u] |= (uint8_t)(1u << (i % 8u));
  }
  send_frame(response, (uint16_t)(3u + byte_count));
}

static void process_frame(void)
{
  uint16_t received_crc;
  uint16_t calculated_crc;
  uint8_t function = s_rx[1];

  if (s_rx_size < 4u) return;
  received_crc = (uint16_t)(s_rx[s_rx_size - 2u] | ((uint16_t)s_rx[s_rx_size - 1u] << 8));
  calculated_crc = crc16(s_rx, (uint16_t)(s_rx_size - 2u));
  if (received_crc != calculated_crc) return;

  s_last_request_ms = HAL_GetTick();
  s_link_seen = 1u;

  switch (function)
  {
    case FC_READ_COILS:
      handle_read_coils();
      break;
    case FC_READ_HOLDING_REGISTERS:
    case FC_READ_INPUT_REGISTERS:
      handle_read_registers(function);
      break;
    case FC_WRITE_SINGLE_COIL:
    {
      uint16_t address = be16(&s_rx[2]);
      uint16_t value = be16(&s_rx[4]);
      if (value != 0x0000u && value != 0xFF00u)
      {
        send_exception(function, 0x03u);
      }
      else if (!write_coil(address, value == 0xFF00u))
      {
        send_exception(function, 0x02u);
      }
      else
      {
        Console_WriteRaw(s_rx, 8u);
      }
      break;
    }
    case FC_WRITE_SINGLE_REGISTER:
      if (!write_holding(be16(&s_rx[2]), be16(&s_rx[4])))
      {
        send_exception(function, 0x02u);
      }
      else
      {
        Console_WriteRaw(s_rx, 8u);
      }
      break;
    case FC_WRITE_MULTIPLE_COILS:
    case FC_WRITE_MULTIPLE_REGS:
      send_exception(function, 0x01u);
      break;
    default:
      send_exception(function, 0x01u);
      break;
  }
}

void ModbusSlave_Init(void)
{
  s_rx_size = 0u;
  s_expected_size = 0u;
  s_last_byte_ms = HAL_GetTick();
  s_last_request_ms = s_last_byte_ms;
  s_left_target_tenth_rpm = 0;
  s_right_target_tenth_rpm = 0;
  s_motor_enabled = 0u;
  s_emergency_stop = 0u;
  s_link_seen = 0u;
}

void ModbusSlave_SetMotorEnabled(uint8_t enabled)
{
  if (enabled && ChargeControl_Requested()) enabled = 0u;
  s_motor_enabled = enabled ? 1u : 0u;
  if (s_motor_enabled)
  {
    RobotButton_SetEnabledIndicator(1u);
    apply_motor_targets();
  }
  else
  {
    s_left_target_tenth_rpm = 0;
    s_right_target_tenth_rpm = 0;
    Docking_Stop();
    MotorControl_Stop();
    RobotButton_SetEnabledIndicator(0u);
  }
}

uint8_t ModbusSlave_MotorEnabled(void)
{
  return s_motor_enabled;
}

uint8_t ModbusSlave_EmergencyStop(void)
{
  return s_emergency_stop;
}

bool ModbusSlave_FeedByte(uint8_t value)
{
  if (s_rx_size == 0u)
  {
    if (value != MODBUS_ADDRESS) return false;
    s_rx[s_rx_size++] = value;
    s_expected_size = 0u;
    s_last_byte_ms = HAL_GetTick();
    return true;
  }

  if (s_rx_size >= MODBUS_FRAME_MAX)
  {
    s_rx_size = 0u;
    s_expected_size = 0u;
    return true;
  }

  s_rx[s_rx_size++] = value;
  s_last_byte_ms = HAL_GetTick();
  if (s_rx_size == 2u)
  {
    switch (s_rx[1])
    {
      case FC_READ_COILS:
      case FC_READ_HOLDING_REGISTERS:
      case FC_READ_INPUT_REGISTERS:
      case FC_WRITE_SINGLE_COIL:
      case FC_WRITE_SINGLE_REGISTER:
        s_expected_size = 8u;
        break;
      case FC_WRITE_MULTIPLE_COILS:
      case FC_WRITE_MULTIPLE_REGS:
        s_expected_size = 0u;
        break;
      default:
        s_expected_size = 8u;
        break;
    }
  }
  if (s_rx_size == 7u &&
      (s_rx[1] == FC_WRITE_MULTIPLE_COILS || s_rx[1] == FC_WRITE_MULTIPLE_REGS))
  {
    s_expected_size = (uint16_t)(9u + s_rx[6]);
    if (s_expected_size > MODBUS_FRAME_MAX)
    {
      s_rx_size = 0u;
      s_expected_size = 0u;
      return true;
    }
  }
  if (s_expected_size != 0u && s_rx_size >= s_expected_size)
  {
    process_frame();
    s_rx_size = 0u;
    s_expected_size = 0u;
  }
  return true;
}

void ModbusSlave_Task(void)
{
  MotorStatus left;
  MotorStatus right;
  uint32_t now = HAL_GetTick();

  if (s_rx_size != 0u && (now - s_last_byte_ms) > MODBUS_FRAME_GAP_MS)
  {
    s_rx_size = 0u;
    s_expected_size = 0u;
  }

  MotorControl_GetMotorStatus(MOTOR_LEFT, &left);
  MotorControl_GetMotorStatus(MOTOR_RIGHT, &right);
  if (s_motor_enabled && (s_emergency_stop || BumperHit_Active() ||
      left.fault_active || left.fault_latched || right.fault_active || right.fault_latched))
  {
    stop_motors();
  }
  if (s_link_seen && s_motor_enabled && (now - s_last_request_ms) > MODBUS_COMMAND_TIMEOUT_MS)
  {
    stop_motors();
  }
}
