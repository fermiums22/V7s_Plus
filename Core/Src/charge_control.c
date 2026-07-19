/**
  ******************************************************************************
  * @file    charge_control.c
  * @brief   Conservative CC/CV charger-enable PWM loop.
  *
  * This controls the board's existing DC/DC enable input; it is not a
  * replacement for a hardware charger or pack BMS. Limits are deliberately
  * conservative provisional values and require current-limited bench
  * validation before unattended charging.
  ******************************************************************************
  */
#include "charge_control.h"

#include "front_ir_bumper.h"

#include <stdbool.h>
#include <stddef.h>

/* TIM3: 48 MHz / (ARR=2399 + 1) = 20 kHz. PC7/TIM3_CH2 uses active-high
 * PWM1 (from the IOC): CCR2=0 holds the charger-enable input LOW/inactive. */
#define CHARGE_PWM_CHANNEL                TIM_CHANNEL_2
#define CHARGE_CONTROL_PERIOD_MS          20u
#define CHARGE_DUTY_MAX_PERMILLE          900u
#define CHARGE_DUTY_SOFTSTART_STEP         10u
#define CHARGE_DUTY_CC_STEP_UP               4u
#define CHARGE_DUTY_CC_STEP_DOWN             8u
#define CHARGE_DUTY_CV_STEP                   5u

/* Provisional 4S Li-ion safety clamps, not a chemistry declaration. */
#define CHARGE_DOCK_ENTER_MV             9000u
#define CHARGE_DOCK_EXIT_MV              7500u
#define CHARGE_DOCK_MAX_MV              28000u
#define CHARGE_DOCK_STABLE_MS            1000u
#define CHARGE_VBAT_CV_MV               16400u
#define CHARGE_VBAT_DONE_MV             16600u
#define CHARGE_VBAT_HARD_MAX_MV         16800u
#define CHARGE_TARGET_CURRENT_MA          500u
#define CHARGE_HARD_CURRENT_MA            800u
#define CHARGE_DONE_CURRENT_MA            100u
#define CHARGE_DONE_HOLD_MS             60000u

/* Status register bit allocation. */
#define CHARGE_STATUS_REQUESTED         0x0001u
#define CHARGE_STATUS_OUTPUT_ACTIVE     0x0002u
#define CHARGE_STATUS_DOCK_PRESENT      0x0004u
#define CHARGE_STATUS_FAULT             0x0008u
#define CHARGE_STATUS_DONE              0x0010u
#define CHARGE_STATUS_AUTO_ENABLED      0x0020u

static TIM_HandleTypeDef *s_pwm;
static uint32_t s_last_control_ms;
static uint32_t s_done_since_ms;
static uint16_t s_duty_permille;
static uint16_t s_vbat_mv;
static int16_t s_current_ma;
static uint16_t s_vin_mv;
static uint16_t s_status;
static uint16_t s_sequence;
static uint8_t s_requested;       /* explicit HA/manual override */
static uint8_t s_auto_enabled;    /* STM-local default: charge on dock */
static uint8_t s_dock_present;
static uint8_t s_dock_candidate;
static uint32_t s_dock_candidate_since_ms;
static ChargeControlState s_state = CHARGE_CONTROL_DISABLED;
static ChargeFault s_fault = CHARGE_FAULT_NONE;

static int16_t clamp_i16(int32_t value)
{
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return (int16_t)value;
}

static uint16_t clamp_u16(uint32_t value)
{
  return (value > 65535u) ? 65535u : (uint16_t)value;
}

static void set_duty(uint16_t duty_permille)
{
  uint32_t counts;
  uint32_t period_counts;

  if (duty_permille > CHARGE_DUTY_MAX_PERMILLE)
    duty_permille = CHARGE_DUTY_MAX_PERMILLE;
  s_duty_permille = duty_permille;
  if (s_pwm == NULL) return;

  period_counts = __HAL_TIM_GET_AUTORELOAD(s_pwm) + 1u;
  counts = ((uint32_t)duty_permille * period_counts + 500u) / 1000u;
  /* Keep channel 2 running. HAL_TIM_PWM_Stop() can leave the pin in an
   * indeterminate peripheral state; CCR2=0 is the verified inactive LOW. */
  __HAL_TIM_SET_COMPARE(s_pwm, CHARGE_PWM_CHANNEL, counts);
}

static void refresh_status(void)
{
  s_status = 0u;
  if (s_requested || (s_auto_enabled && s_dock_present)) s_status |= CHARGE_STATUS_REQUESTED;
  if (s_duty_permille != 0u) s_status |= CHARGE_STATUS_OUTPUT_ACTIVE;
  if (s_dock_present) s_status |= CHARGE_STATUS_DOCK_PRESENT;
  if (s_fault != CHARGE_FAULT_NONE) s_status |= CHARGE_STATUS_FAULT;
  if (s_state == CHARGE_CONTROL_DONE) s_status |= CHARGE_STATUS_DONE;
  if (s_auto_enabled) s_status |= CHARGE_STATUS_AUTO_ENABLED;
}

static uint8_t charge_requested(void)
{
  return (s_requested || (s_auto_enabled && s_dock_present)) ? 1u : 0u;
}

static void stop_output(void)
{
  set_duty(0u); /* PC7 low: verified inactive polarity. */
}

static void enter_fault(ChargeFault fault)
{
  stop_output();
  s_requested = 0u;
  s_auto_enabled = 0u; /* hard faults must never auto-retry unattended */
  s_fault = fault;
  s_state = CHARGE_CONTROL_FAULT;
  s_done_since_ms = 0u;
  s_sequence++;
  refresh_status();
}

static bool read_measurements(void)
{
  uint16_t vbat_raw;
  uint16_t current_raw;
  uint16_t vin_raw;
  int32_t pin_mv;

  /* These are the same full-half-buffer means published to HA. Do not use the
   * short DMA-ring snapshot here: it can be read during a writer advance and
   * has produced implausible B4/B6 values despite valid published telemetry. */
  vbat_raw = g_vbat_sense_adc;
  current_raw = g_batt_isense_adc;
  vin_raw = g_vin_sense_adc;

  /* PA2 and PA7 cannot normally be zero on a powered pack. Vin=0 is allowed:
   * it represents no dock and must safely keep the state machine in WAIT_DOCK.
   * A rail at full ADC scale is treated as an invalid/saturated measurement. */
  if (vbat_raw == 0u || current_raw == 0u ||
      vbat_raw >= 4090u || current_raw >= 4090u || vin_raw >= 4090u)
    return false;

  s_vbat_mv = clamp_u16(FrontIrBumper_VbatPackMilliVolts());
  s_vin_mv = clamp_u16(FrontIrBumper_VinRailMilliVolts());
  pin_mv = (int32_t)FrontIrBumper_BattMilliVolts();
  /* The established PA7 calibration says + = pack discharge and - = charge. */
  s_current_ma = clamp_i16(((pin_mv - BATT_ISENSE_OFFSET_MV) * 1000) /
                           BATT_ISENSE_MV_PER_A);
  return true;
}

/* PA1 is zero with no dock but can read about 13 V while the real charger is
 * loaded.  Debounce its presence independently of HA/S3 and use hysteresis so
 * the dock cannot chatter the PC7 enable line. */
static void update_dock_present(uint32_t now)
{
  uint16_t threshold = s_dock_present ? CHARGE_DOCK_EXIT_MV : CHARGE_DOCK_ENTER_MV;
  uint8_t candidate = (s_vin_mv >= threshold && s_vin_mv <= CHARGE_DOCK_MAX_MV) ? 1u : 0u;

  if (candidate != s_dock_candidate)
  {
    s_dock_candidate = candidate;
    s_dock_candidate_since_ms = now;
    return;
  }
  if (candidate != s_dock_present &&
      (uint32_t)(now - s_dock_candidate_since_ms) >= CHARGE_DOCK_STABLE_MS)
  {
    s_dock_present = candidate;
    s_sequence++;
  }
}

static uint16_t charging_current_ma(void)
{
  return (s_current_ma < 0) ? (uint16_t)(-(int32_t)s_current_ma) : 0u;
}

static void control_constant_current(void)
{
  uint16_t charge_ma = charging_current_ma();

  if (charge_ma + 25u < CHARGE_TARGET_CURRENT_MA)
    set_duty((uint16_t)(s_duty_permille + CHARGE_DUTY_CC_STEP_UP));
  else if (charge_ma > CHARGE_TARGET_CURRENT_MA + 25u)
    set_duty((s_duty_permille > CHARGE_DUTY_CC_STEP_DOWN) ?
             (uint16_t)(s_duty_permille - CHARGE_DUTY_CC_STEP_DOWN) : 0u);
}

static void control_constant_voltage(uint32_t now)
{
  uint16_t charge_ma = charging_current_ma();

  if (s_vbat_mv + 20u < CHARGE_VBAT_DONE_MV)
    set_duty((uint16_t)(s_duty_permille + CHARGE_DUTY_CV_STEP));
  else if (s_vbat_mv > CHARGE_VBAT_DONE_MV)
    set_duty((s_duty_permille > CHARGE_DUTY_CV_STEP) ?
             (uint16_t)(s_duty_permille - CHARGE_DUTY_CV_STEP) : 0u);

  if (s_vbat_mv >= CHARGE_VBAT_DONE_MV && charge_ma <= CHARGE_DONE_CURRENT_MA)
  {
    if (s_done_since_ms == 0u) s_done_since_ms = now;
    if ((uint32_t)(now - s_done_since_ms) >= CHARGE_DONE_HOLD_MS)
    {
      stop_output();
      s_requested = 0u;
      s_state = CHARGE_CONTROL_DONE;
      s_sequence++;
    }
  }
  else
  {
    s_done_since_ms = 0u;
  }
}

void ChargeControl_Init(TIM_HandleTypeDef *pwm_timer)
{
  s_pwm = pwm_timer;
  s_requested = 0u;
  s_auto_enabled = 1u;
  s_fault = CHARGE_FAULT_NONE;
  s_state = CHARGE_CONTROL_DISABLED;
  s_duty_permille = 0u;
  s_done_since_ms = 0u;
  s_last_control_ms = HAL_GetTick();
  s_dock_present = 0u;
  s_dock_candidate = 0u;
  s_dock_candidate_since_ms = s_last_control_ms;
  if (s_pwm != NULL)
  {
    /* Program safe CCR2 before enabling PC7 alternate-function output. */
    __HAL_TIM_SET_COMPARE(s_pwm, CHARGE_PWM_CHANNEL, 0u);
    (void)HAL_TIM_PWM_Start(s_pwm, CHARGE_PWM_CHANNEL);
  }
  refresh_status();
}

void ChargeControl_Start(void)
{
  if (s_pwm == NULL) return;
  stop_output();
  s_requested = 1u;
  s_fault = CHARGE_FAULT_NONE;
  s_state = CHARGE_CONTROL_WAIT_DOCK;
  s_done_since_ms = 0u;
  s_sequence++;
  refresh_status();
}

void ChargeControl_Stop(void)
{
  stop_output();
  s_requested = 0u;
  if (!s_auto_enabled) s_state = CHARGE_CONTROL_DISABLED;
  s_done_since_ms = 0u;
  s_sequence++;
  refresh_status();
}

void ChargeControl_SetAutoEnabled(uint8_t enabled)
{
  s_auto_enabled = enabled ? 1u : 0u;
  if (s_auto_enabled)
  {
    s_fault = CHARGE_FAULT_NONE;
    if (s_state == CHARGE_CONTROL_FAULT || s_state == CHARGE_CONTROL_DISABLED)
      s_state = CHARGE_CONTROL_WAIT_DOCK;
  }
  else if (!s_requested)
  {
    stop_output();
    s_state = CHARGE_CONTROL_DISABLED;
  }
  s_done_since_ms = 0u;
  s_sequence++;
  refresh_status();
}

void ChargeControl_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint16_t charge_ma;

  if ((uint32_t)(now - s_last_control_ms) < CHARGE_CONTROL_PERIOD_MS) return;
  s_last_control_ms = now;

  if (!read_measurements())
  {
    if (charge_requested() || s_duty_permille != 0u) enter_fault(CHARGE_FAULT_SENSOR);
    else refresh_status();
    return;
  }
  if (s_vbat_mv >= CHARGE_VBAT_HARD_MAX_MV)
  {
    if (charge_requested() || s_duty_permille != 0u) enter_fault(CHARGE_FAULT_OVERVOLTAGE);
    else refresh_status();
    return;
  }
  if (s_vin_mv > CHARGE_DOCK_MAX_MV)
  {
    if (charge_requested() || s_duty_permille != 0u) enter_fault(CHARGE_FAULT_VIN_RANGE);
    else refresh_status();
    return;
  }
  charge_ma = charging_current_ma();
  if (charge_ma > CHARGE_HARD_CURRENT_MA)
  {
    if (charge_requested() || s_duty_permille != 0u) enter_fault(CHARGE_FAULT_OVERCURRENT);
    else refresh_status();
    return;
  }

  update_dock_present(now);

  /* A completed cycle is terminal only while the robot remains on the dock.
   * Removing it arms the next autonomous dock/charge cycle. */
  if (!s_dock_present && s_state == CHARGE_CONTROL_DONE)
  {
    s_state = CHARGE_CONTROL_WAIT_DOCK;
    s_done_since_ms = 0u;
  }

  if (s_fault != CHARGE_FAULT_NONE)
  {
    stop_output();
    refresh_status();
    return;
  }
  if (!charge_requested())
  {
    stop_output();
    if (s_state != CHARGE_CONTROL_DONE)
      s_state = CHARGE_CONTROL_DISABLED;
    refresh_status();
    return;
  }
  if (!s_dock_present)
  {
    stop_output();
    s_state = CHARGE_CONTROL_WAIT_DOCK;
    s_done_since_ms = 0u;
    refresh_status();
    return;
  }

  if (s_state == CHARGE_CONTROL_DONE)
  {
    stop_output();
    refresh_status();
    return;
  }
  if (s_state == CHARGE_CONTROL_DISABLED)
  {
    s_state = CHARGE_CONTROL_WAIT_DOCK;
  }

  switch (s_state)
  {
    case CHARGE_CONTROL_WAIT_DOCK:
      set_duty(0u);
      s_state = CHARGE_CONTROL_SOFT_START;
      break;
    case CHARGE_CONTROL_SOFT_START:
      set_duty((uint16_t)(s_duty_permille + CHARGE_DUTY_SOFTSTART_STEP));
      if (s_duty_permille >= 100u) s_state = CHARGE_CONTROL_CONSTANT_CURRENT;
      break;
    case CHARGE_CONTROL_CONSTANT_CURRENT:
      if (s_vbat_mv >= CHARGE_VBAT_CV_MV)
      {
        s_state = CHARGE_CONTROL_CONSTANT_VOLTAGE;
        s_done_since_ms = 0u;
      }
      else
        control_constant_current();
      break;
    case CHARGE_CONTROL_CONSTANT_VOLTAGE:
      control_constant_voltage(now);
      break;
    default:
      /* Start() is the only transition out of terminal states. */
      break;
  }
  refresh_status();
}

uint8_t ChargeControl_Requested(void) { return s_requested; }
uint8_t ChargeControl_AutoEnabled(void) { return s_auto_enabled; }
uint8_t ChargeControl_OutputActive(void) { return (s_duty_permille != 0u) ? 1u : 0u; }
uint8_t ChargeControl_DockPresent(void) { return s_dock_present; }
uint16_t ChargeControl_Status(void) { return s_status; }
uint16_t ChargeControl_Sequence(void) { return s_sequence; }
ChargeControlState ChargeControl_State(void) { return s_state; }
ChargeFault ChargeControl_Fault(void) { return s_fault; }
uint16_t ChargeControl_DutyPermille(void) { return s_duty_permille; }
uint16_t ChargeControl_VbatMilliVolts(void) { return s_vbat_mv; }
int16_t ChargeControl_BatteryCurrentMilliAmps(void) { return s_current_ma; }
uint16_t ChargeControl_VinMilliVolts(void) { return s_vin_mv; }
