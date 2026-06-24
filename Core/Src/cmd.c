/**
  ******************************************************************************
  * @file    cmd.c
  * @brief   Command-line parser + telemetry for the V7s Plus robot base.
  *
  * Commands:
  *   help / logo / status / reset
  *   ir [on|off] [Hz]      front IR: per-zone proximity (signal) + approach (rate)
  *   stream on|off [Hz]    same IR telemetry as a CSV stream
  *   thr [near] [move]     tune near/approach thresholds
  *   rate <ms>             stream period
  *
  * Front IR per zone (R/F/L): signal = reflected-IR (grows when an object is
  * closer); rate = change since last update (~15 Hz; +approaching, -receding) ->
  * lets the robot notice MOVING obstacles (legs, dog) not just static ones.
  ******************************************************************************
  */
#include "main.h"
#include "cmd.h"
#include "console.h"
#include "front_ir_bumper.h"
#include "buzzer.h"
#include "cliff_ir.h"
#include "base_ir.h"
#include "bumper_hit.h"
#include "motor_control.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CMD_BUF_SIZE   (96u)
#define HIST_N         (8u)
#define NCON           CONSOLE_NPORT

typedef struct {
  char     cmd_buf[CMD_BUF_SIZE];
  uint16_t cmd_len;
  char     hist[HIST_N][CMD_BUF_SIZE];
  int      hist_count;
  int      hist_nav;
  uint8_t  esc_state;                   /* 0:normal 1:ESC 2:'[' */
  bool     stream_on;
  uint32_t stream_rate;                 /* [ms] */
  uint32_t stream_last;
} cmd_ctx_t;

static cmd_ctx_t g_ctx[NCON];
static cmd_ctx_t *g_cur = &g_ctx[0];

/* Tunable detector thresholds (live via 'thr'). */
static int s_near_sig  = 700;           /* signal above this = object NEAR in zone */
static int s_move_rate = 60;            /* |rate| above this  = motion in zone     */

static bool set_rate_hz(const char *s)
{
  long hz = strtol(s, NULL, 10);
  if (hz < 1 || hz > 500) return false;
  uint32_t ms = (uint32_t)(1000 / hz);
  if (ms < 1) ms = 1;
  g_cur->stream_rate = ms;
  return true;
}

void Cmd_Banner(void)
{
  Console_Print("\r\n");
  Console_Print("  #   #  #####    ###\r\n");
  Console_Print("  #   #      #   #\r\n");
  Console_Print("  #   #    ##     ###\r\n");
  Console_Print("   # #    #          #\r\n");
  Console_Print("    #     #####   ###  s+\r\n");
  Console_Print("=====================================\r\n");
  Console_Print("  V7s Plus robot base (STM32F071)\r\n");
  Console_Print("  wheels | IR bumper | cliff | hit | buzzer\r\n");
  Console_Print("=====================================\r\n");
  Console_Print("Type 'help'.\r\n");
}

/* One-word verdict per zone from signal + rate. */
static const char *zone_verdict(int sig, int rate)
{
  if (rate >=  s_move_rate) return (sig >= s_near_sig) ? "APPROACH!" : "approaching";
  if (rate <= -s_move_rate) return "receding";
  if (sig  >=  s_near_sig)  return "NEAR";
  return "clear";
}

static void cmd_help(void)
{
  Console_Print("Commands:\r\n");
  Console_Print("  help                 - this help\r\n");
  Console_Print("  logo                 - show the banner\r\n");
  Console_Print("  status / ir          - front IR zones: signal(proximity) + rate(approach)\r\n");
  Console_Print("  cliff                - 4 cliff + side IR: signal + rate (all 5 live)\r\n");
  Console_Print("  baseir               - 5 dock-beacon IR rx: per-dir strength + dock dir\r\n");
  Console_Print("  batt                 - battery current sense voltage at PA7 (adc + mV)\r\n");
  Console_Print("  fwd|back [rev]        - roll both wheels N revs (default 1) @80rpm\r\n");
  Console_Print("  spin [rpm]           - RIGHT wheel continuous spin (default 60; brake by hand)\r\n");
  Console_Print("  mstop                - stop the wheels\r\n");
  Console_Print("  hit                  - bumper-hit L/R (PB5/PE12) EXTI-latched; read clears\r\n");
  Console_Print("  ir on|off [Hz]       - stream the IR telemetry (alias of stream)\r\n");
  Console_Print("  stream on|off [Hz]   - IR,<ms>,<R>,<F>,<L>,<dR>,<dF>,<dL>  (sig + rate)\r\n");
  Console_Print("  thr [near] [move]    - get/set NEAR signal + MOVE rate thresholds\r\n");
  Console_Print("  rate <ms>            - stream period (alt to [Hz])\r\n");
  Console_Print("  play [name|n]        - play an 8-bit melody (no arg = list)\r\n");
  Console_Print("  beep [Hz] [ms]       - single tone (default 1000 Hz, 120 ms)\r\n");
  Console_Print("  stop                 - silence the buzzer\r\n");
  Console_Print("  illum on|off         - shared Q11/PB10 IR illumination carrier on/off\r\n");
  Console_Print("  reset | reboot       - restart\r\n");
  Console_Print("  (Up/Down = history; Ctrl+C / Ctrl+Z = stop stream)\r\n");
}

static void cmd_ir_once(void)
{
  char b[160];
  int rs = g_front_ir_r_signal, fs = g_front_ir_f_signal, ls = g_front_ir_l_signal;
  int rr = g_front_ir_r_rate,   fr = g_front_ir_f_rate,   lr = g_front_ir_l_rate;
  snprintf(b, sizeof b, "IR  L=%d(%+d) %-11s | F=%d(%+d) %-11s | R=%d(%+d) %-11s\r\n",
           ls, lr, zone_verdict(ls, lr),
           fs, fr, zone_verdict(fs, fr),
           rs, rr, zone_verdict(rs, rr));
  Console_Print(b);
}

static void cmd_cliff_once(void)
{
  char b[160];
  snprintf(b, sizeof b,
           "CLIFF FL=%d(%+d) FR=%d(%+d) RR=%d(%+d) | LL=%d(%+d) SIDE=%d(%+d)\r\n",
           g_cliff_signal[CLIFF_FRONT_L], g_cliff_rate[CLIFF_FRONT_L],
           g_cliff_signal[CLIFF_FRONT_R], g_cliff_rate[CLIFF_FRONT_R],
           g_cliff_signal[CLIFF_RIGHT],   g_cliff_rate[CLIFF_RIGHT],
           g_cliff_signal[CLIFF_LEFT],    g_cliff_rate[CLIFF_LEFT],
           g_cliff_signal[SIDE_IR],       g_cliff_rate[SIDE_IR]);
  Console_Print(b);
}

static void cmd_baseir_once(void)
{
  char b[120];
  int dir = BaseIr_Direction();
  const char *dname = (dir >= 0) ? BaseIr_Name(dir) : "-";
  snprintf(b, sizeof b,
           "BASEIR FL=%u FR=%u L=%u R=%u RR=%u (permille low) -> dock=%s\r\n",
           (unsigned)g_base_ir_activity[BASE_IR_FRONT_L], (unsigned)g_base_ir_activity[BASE_IR_FRONT_R],
           (unsigned)g_base_ir_activity[BASE_IR_LEFT],    (unsigned)g_base_ir_activity[BASE_IR_RIGHT],
           (unsigned)g_base_ir_activity[BASE_IR_REAR],    dname);
  Console_Print(b);
}

static void cmd_status(void)
{
  cmd_ir_once();
  char b[96];
  snprintf(b, sizeof b, "    thr: near=%d move=%d | stream=%s @%lums\r\n",
           s_near_sig, s_move_rate, g_cur->stream_on ? "on" : "off",
           (unsigned long)g_cur->stream_rate);
  Console_Print(b);
}

static void stream_on(const char *hz)
{
  if (hz && !set_rate_hz(hz)) { Console_Print("ERR rate must be 1..500 Hz\r\n"); return; }
  g_cur->stream_on = true;
  g_cur->stream_last = HAL_GetTick();
  Console_Print("OK stream on (Ctrl+C/Ctrl+Z to stop)\r\n");
}

static void cmd_dispatch(char *line)
{
  for (char *p = line; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 32;

  char *tok = strtok(line, " \t");
  if (!tok) return;

  char cmd[16];
  int ci = 0;
  while (((tok[ci] >= 'a' && tok[ci] <= 'z') || tok[ci] == '_') && ci < 15) { cmd[ci] = tok[ci]; ci++; }
  cmd[ci] = '\0';
  char *arg = (tok[ci] != '\0') ? &tok[ci] : strtok(NULL, " \t");
  char b[96];

  if (!strcmp(cmd, "help"))   { cmd_help();   return; }
  if (!strcmp(cmd, "logo"))   { Cmd_Banner(); return; }
  if (!strcmp(cmd, "status")) { cmd_status(); return; }
  if (!strcmp(cmd, "cliff"))  { cmd_cliff_once(); return; }
  if (!strcmp(cmd, "baseir")) { cmd_baseir_once(); return; }

  if (!strcmp(cmd, "batt"))   /* PA7/ADC_IN7 = U4 OUT2 battery current sense voltage */
  {
    snprintf(b, sizeof b, "BATT PA7 adc=%u  %u mV  (U4 OUT2 batt-current sense)\r\n",
             (unsigned)g_batt_isense_adc, (unsigned)FrontIrBumper_BattMilliVolts());
    Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "reset") || !strcmp(cmd, "reboot"))
  {
    Console_Print("OK reboot\r\n");
    HAL_Delay(20);
    NVIC_SystemReset();
  }

  if (!strcmp(cmd, "ir"))
  {
    if (!arg) { cmd_ir_once(); return; }
    if (!strcmp(arg, "on"))  { stream_on(strtok(NULL, " \t")); return; }
    if (!strcmp(arg, "off")) { g_cur->stream_on = false; Console_Print("OK stream off\r\n"); return; }
    stream_on(arg);                        /* "ir 20" -> stream at 20 Hz */
    return;
  }

  if (!strcmp(cmd, "stream"))
  {
    if (arg && !strcmp(arg, "on"))  { stream_on(strtok(NULL, " \t")); return; }
    if (arg && !strcmp(arg, "off")) { g_cur->stream_on = false; Console_Print("OK stream off\r\n"); return; }
    Console_Print("ERR stream on|off [Hz]\r\n");
    return;
  }

  if (!strcmp(cmd, "thr"))
  {
    if (arg)
    {
      long n = strtol(arg, NULL, 10);
      if (n > 0) s_near_sig = (int)n;
      char *p2 = strtok(NULL, " \t");
      if (p2) { long m = strtol(p2, NULL, 10); if (m > 0) s_move_rate = (int)m; }
    }
    snprintf(b, sizeof b, "OK thr near=%d move=%d\r\n", s_near_sig, s_move_rate);
    Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "rate"))
  {
    if (!arg) { Console_Print("ERR rate <ms>\r\n"); return; }
    long r = strtol(arg, NULL, 10);
    if (r < 1) r = 1;
    g_cur->stream_rate = (uint32_t)r;
    snprintf(b, sizeof b, "OK rate %ld ms\r\n", r); Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "play"))
  {
    if (!arg)
    {
      Console_Print("melodies:");
      for (int i = 0; i < Buzzer_MelodyCount(); i++)
      { Console_Print(" "); Console_Print(Buzzer_MelodyName(i)); }
      Console_Print("\r\n");
      return;
    }
    int mi = Buzzer_MelodyByName(arg);
    if (mi < 0)
    {
      long n = strtol(arg, NULL, 10);
      if (n >= 0 && n < Buzzer_MelodyCount() && (arg[0] >= '0' && arg[0] <= '9')) mi = (int)n;
    }
    if (mi < 0) { Console_Print("ERR no such melody (try 'play')\r\n"); return; }
    Buzzer_Play((BuzzerMelody)mi);
    snprintf(b, sizeof b, "OK play %s\r\n", Buzzer_MelodyName(mi)); Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "beep"))
  {
    long f = 1000, ms = 120;
    if (arg)
    {
      f = strtol(arg, NULL, 10);
      char *p2 = strtok(NULL, " \t");
      if (p2) ms = strtol(p2, NULL, 10);
    }
    if (f < 50)   f = 50;
    if (f > 5000) f = 5000;
    if (ms < 1)    ms = 1;
    if (ms > 5000) ms = 5000;
    Buzzer_Tone((uint16_t)f, (uint16_t)ms);
    snprintf(b, sizeof b, "OK beep %ldHz %ldms\r\n", f, ms); Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "stop")) { Buzzer_Stop(); Console_Print("OK stop\r\n"); return; }

  if (!strcmp(cmd, "illum"))   /* Q11 IR illumination carrier (PB10) on/off */
  {
    if (arg && !strcmp(arg, "on"))  { FrontIrBumper_SetIllumination(1); Console_Print("OK illum: Q11 carrier ON\r\n"); return; }
    if (arg && !strcmp(arg, "off")) { FrontIrBumper_SetIllumination(0); Console_Print("OK illum: Q11 carrier OFF (PB10 low) - measure shunts\r\n"); return; }
    Console_Print("ERR illum on|off  (Q11/PB10 shared IR illumination carrier)\r\n");
    return;
  }

  if (!strcmp(cmd, "fwd") || !strcmp(cmd, "back"))   /* roll both wheels N revs */
  {
    long revs = 1;
    if (arg) { long v = strtol(arg, NULL, 10); if (v >= 1 && v <= 50) revs = v; }
    float d = (cmd[0] == 'b') ? -(float)revs : (float)revs;
    MotorControl_MoveWheelRelative(d, 80.0f);
    MotorControl_MoveRightWheelRelative(d, 80.0f);
    snprintf(b, sizeof b, "OK %s %ld rev @80rpm ('mstop' to halt)\r\n", cmd, revs);
    Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "mstop")) { MotorControl_Stop(); Console_Print("OK motors stop\r\n"); return; }

  if (!strcmp(cmd, "spin"))   /* RIGHT wheel continuous spin (bench current test; brake by hand) */
  {
    long rpm = 60;
    if (arg) { long v = strtol(arg, NULL, 10); if (v >= -300 && v <= 300 && v != 0) rpm = v; }
    MotorControl_SetRightWheelSpeed((float)rpm);
    snprintf(b, sizeof b, "OK spin RIGHT @%ld rpm ('mstop' to halt)\r\n", rpm);
    Console_Print(b);
    return;
  }

  if (!strcmp(cmd, "hit"))   /* bumper-hit impact status (EXTI-latched), then clear */
  {
    uint8_t ev = BumperHit_Events();
    snprintf(b, sizeof b, "HIT latched L=%d R=%d | now L=%d R=%d  (idle high; read clears)\r\n",
             (ev >> BUMPER_HIT_LEFT) & 1, (ev >> BUMPER_HIT_RIGHT) & 1,
             BumperHit_Level(BUMPER_HIT_LEFT), BumperHit_Level(BUMPER_HIT_RIGHT));
    Console_Print(b);
    BumperHit_Clear();
    return;
  }

  Console_Print("ERR unknown cmd (try help)\r\n");
}

void Cmd_Init(void)
{
  for (int i = 0; i < NCON; i++)
  {
    g_ctx[i].cmd_len     = 0;
    g_ctx[i].stream_on   = false;
    g_ctx[i].stream_rate = 100;
    g_ctx[i].stream_last = 0;
    g_ctx[i].hist_count  = 0;
    g_ctx[i].hist_nav    = 0;
    g_ctx[i].esc_state   = 0;
  }
  g_cur = &g_ctx[0];
}

static void line_redraw(void)
{
  g_cur->cmd_buf[g_cur->cmd_len] = '\0';
  Console_Print("\r\033[K");
  if (g_cur->cmd_len) Console_Print(g_cur->cmd_buf);
}

static void hist_store(const char *s)
{
  cmd_ctx_t *cx = g_cur;
  if (s[0] == '\0') return;
  if (cx->hist_count > 0 && !strcmp(cx->hist[cx->hist_count - 1], s)) return;
  if (cx->hist_count < (int)HIST_N)
  {
    strncpy(cx->hist[cx->hist_count], s, CMD_BUF_SIZE - 1);
    cx->hist[cx->hist_count][CMD_BUF_SIZE - 1] = '\0';
    cx->hist_count++;
  }
  else
  {
    for (int k = 1; k < (int)HIST_N; k++) strcpy(cx->hist[k - 1], cx->hist[k]);
    strncpy(cx->hist[HIST_N - 1], s, CMD_BUF_SIZE - 1);
    cx->hist[HIST_N - 1][CMD_BUF_SIZE - 1] = '\0';
  }
}

static void hist_recall(int idx)
{
  strcpy(g_cur->cmd_buf, g_cur->hist[idx]);
  g_cur->cmd_len = (uint16_t)strlen(g_cur->cmd_buf);
  line_redraw();
}

static void feed_one(char c)
{
  cmd_ctx_t *cx = g_cur;

  if (c == 0x03 || c == 0x1A)            /* Ctrl+C / Ctrl+Z: stop stream */
  {
    if (cx->stream_on) { cx->stream_on = false; Console_Print("\r\n[stream stopped]\r\n"); }
    cx->cmd_len = 0; cx->esc_state = 0;
    return;
  }

  if (cx->esc_state == 1) { cx->esc_state = (c == '[') ? 2 : 0; return; }
  if (cx->esc_state == 2)
  {
    if (c == 'A') { if (cx->hist_nav > 0) { cx->hist_nav--; hist_recall(cx->hist_nav); } }
    else if (c == 'B') { if (cx->hist_nav < cx->hist_count) { cx->hist_nav++;
                          if (cx->hist_nav == cx->hist_count) { cx->cmd_len = 0; line_redraw(); }
                          else hist_recall(cx->hist_nav); } }
    cx->esc_state = 0;
    return;
  }
  if (c == 0x1B) { cx->esc_state = 1; return; }

  if (c == '\r' || c == '\n')
  {
    Console_Print("\r\n");
    if (cx->cmd_len)
    {
      cx->cmd_buf[cx->cmd_len] = '\0';
      hist_store(cx->cmd_buf);
      cmd_dispatch(cx->cmd_buf);
      cx->cmd_len = 0;
    }
    cx->hist_nav = cx->hist_count;
  }
  else if (c == '\b' || c == 0x7F)
  {
    if (cx->cmd_len) { cx->cmd_len--; Console_Print("\b \b"); }
  }
  else if ((unsigned char)c >= 0x20)
  {
    if (cx->cmd_len < (CMD_BUF_SIZE - 1))
    {
      cx->cmd_buf[cx->cmd_len++] = c;
      char e[2] = { c, '\0' };
      Console_Print(e);
    }
  }
}

void Cmd_FeedByte(int port, char c)
{
  if (port < 0 || port >= NCON) return;
  g_cur = &g_ctx[port];
  Console_Route(port);
  feed_one(c);
  Console_Route(CONSOLE_BOTH);
}

void Cmd_StreamTask(void)
{
  uint32_t now = HAL_GetTick();
  char line[80];
  for (int p = 0; p < NCON; p++)
  {
    cmd_ctx_t *cx = &g_ctx[p];
    if (!cx->stream_on) continue;
    if ((now - cx->stream_last) < cx->stream_rate) continue;
    cx->stream_last = now;

    Console_Route(p);
    snprintf(line, sizeof line, "IR,%lu,%d,%d,%d,%d,%d,%d\r\n",
             (unsigned long)now,
             g_front_ir_r_signal, g_front_ir_f_signal, g_front_ir_l_signal,
             g_front_ir_r_rate,   g_front_ir_f_rate,   g_front_ir_l_rate);
    Console_Stream(line);
  }
  Console_Route(CONSOLE_BOTH);
}
