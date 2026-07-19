/**
  ******************************************************************************
  * @file    modbus_slave.h
  * @brief   Modbus RTU slave used by the ESP32-S3 robot controller.
  ******************************************************************************
  */
#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

void ModbusSlave_Init(void);
bool ModbusSlave_FeedByte(uint8_t value);
void ModbusSlave_Task(void);
void ModbusSlave_SetMotorEnabled(uint8_t enabled);
uint8_t ModbusSlave_MotorEnabled(void);
uint8_t ModbusSlave_EmergencyStop(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_H */
