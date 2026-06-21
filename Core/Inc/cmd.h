/**
  ******************************************************************************
  * @file    cmd.h
  * @brief   Line-based command parser + telemetry stream scheduler for the
  *          V7s Plus robot base. User-owned.
  ******************************************************************************
  */
#ifndef CMD_H
#define CMD_H

void Cmd_Init(void);
void Cmd_Banner(void);                /* welcome banner + hint */
void Cmd_FeedByte(int port, char c);  /* feed an RX byte: echo + line edit + parse */
void Cmd_StreamTask(void);            /* emit the active telemetry stream */

#endif /* CMD_H */
