/**
  ******************************************************************************
  * @file    ir_remote.c
  * @brief   Timestamp and dispatch dock IR edges to the calibrated decoder.
  ******************************************************************************
  */
#include "ir_remote.h"
#include "base_ir.h"

static TIM_HandleTypeDef *s_tim;

static GPIO_TypeDef *const s_port[] =
{
  BASE_IR_FRONT_L_GPIO_Port, BASE_IR_FRONT_R_GPIO_Port, BASE_IR_LEFT_GPIO_Port,
  BASE_IR_RIGHT_GPIO_Port,   BASE_IR_REAR_GPIO_Port,
};

static const uint16_t s_pin[] =
{
  BASE_IR_FRONT_L_Pin, BASE_IR_FRONT_R_Pin, BASE_IR_LEFT_Pin,
  BASE_IR_RIGHT_Pin,   BASE_IR_REAR_Pin,
};

void IrRemote_Init(TIM_HandleTypeDef *us_timer)
{
  s_tim = us_timer;
  if (s_tim != 0)
  {
    (void)HAL_TIM_Base_Start(s_tim);
  }
}

void IrRemote_OnEdge(IrRxDir ch)
{
  uint16_t now;
  uint8_t level_after;

  if ((unsigned)ch >= BASE_IR_COUNT || s_tim == 0)
  {
    return;
  }

  now = (uint16_t)__HAL_TIM_GET_COUNTER(s_tim);
  level_after = (HAL_GPIO_ReadPin(s_port[ch], s_pin[ch]) == GPIO_PIN_SET) ? 1u : 0u;
  BaseIr_OnEdge((BaseIrDir)ch, level_after, now);
}
