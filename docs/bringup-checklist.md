# V7s Plus Bring-up Checklist

This is the strict working checklist for mapping sensors, removed assemblies, and future I/O.
Update the status after every continuity or powered test.

## Status Legend

- `firmware-ready`: STM32 pin and electrical behavior are known; safe to configure/use.
- `mapped`: connector pins and function are known, firmware not added yet.
- `probing`: current target for continuity/voltage tests.
- `unknown`: not traced yet.
- `removed`: original assembly removed, connector can be repurposed after mapping.

## Already Connected In Firmware

| Status | Connector / node | Function | STM32 / firmware |
|---|---|---|---|
| firmware-ready | `J5` | left wheel motor and encoder | motor driver `U2`, encoder `PD3` |
| firmware-ready | `J14` | right wheel motor and encoder | motor driver `U6`, encoder `PE8` |
| firmware-ready | `Q20/Q21` | common 5 V sensor/encoder rail | `PE5` enables `SWITCHED_SENSOR_5V` |
| firmware-ready | `U2` | left wheel driver | `PB7`, `PC8/TIM3_CH3`, `PD8`, `PB1/ADC_IN9` |
| firmware-ready | `U6` | right wheel driver | `PE13`, `PC6/TIM3_CH1`, `PD14`, `PC5/ADC_IN15` |

## Main Sensor Mapping Queue

| Order | Status | Connector / node | Function | Next exact check |
|---|---|---|---|---|
| 1 | firmware-ready | `J7` | front 8-pin IR navigation panel | DONE: 3 zones R/F/L (PC3/PA5/PB0 = ADC IN13/IN5/IN8). Driver `front_ir_bumper.c`: 1 kHz toggle carrier on PB10/TIM2_CH3, TIM2 TRGO triggers ADC, DMA1_Ch1 circular, synchronous off-on demod. Detects obstacle per-zone, rejects ambient. Next: threshold/event logic |
| 2 | firmware-ready | `J20` + `U10` | front-caster black/white odometry wheel | DONE: J20 1/3=GND, 2=receiver->divider->U10 ch1 Schmitt, 4=emitter via 3.9R from 5V. U10:1 (OUT1) -> `PD2`. Driver `caster_odo.c`: EXTI2 edge counter; console `odo` (count/level + stream). U10 ch2 = overcurrent on Q25 (R100 0.1ohm) - separate, TBD |
| 3 | firmware-ready | onboard front IR receivers | front-left/front-right dock beacon receivers | front-L `PB11`, front-R `PE15` (digital). Now live in `base_ir.c` as part of the 5-way dock-beacon scan (FL/FR/L `PD13`/R `PE6`/RR `PD4`): per-direction low-time strength + `BaseIr_Direction()`. Console: `baseir`. Next: VCC/GND + homing logic |
| 4 | firmware-ready | `J17` | left side IR + optical bumper limit + IR receiver | DONE: pin1=switched 5V sensor/motor pwr, pin2=emitter drive->Q22, pin3=GND, pin4=cliff sense->`PC1`(ADC IN11), pin5=side-trx RX->`PC0`(ADC IN10), pin6=base IR RX->`PD13`, pin7=bumper-hit->`PB5`, pin8=direct 5V. Cliff(PC1)+side(PC0) now in the 8-channel `front_ir_bumper.c` demod scan via .ioc. Next: PD13/PB5 digital read + thresholds |
| 5 | unknown | `J2` | right side IR + optical bumper limit + IR receiver | map GND/VCC and three sub-signals |
| 6 | partial | `J15` | rear IR receiver | OUT mapped: `J15:3` (device pin1, demod OUT) -> series R -> `PD4` (digital input). Still TODO: VCC/GND (`J15:1`/`J15:2`) and any power-enable |
| 7 | firmware-ready (sense) | `J3` | front right cliff IR sensor | sense `J3:3`->`PC4`(ADC IN14), now live in the 8-channel `front_ir_bumper.c` demod. Emitter rides J4->J3->J2 VBAT chain (Q11 carrier). Rest of colodka per cliff 4-pin map |
| 8 | firmware-ready (sense) | `J4` | front left cliff IR sensor | sense `J4:3`->`PC2`(ADC IN12), now live in the 8-channel demod. Emitter = Q4 chain head. Rest of colodka per cliff 4-pin map |
| 9 | unknown | `J12` | touch button with RGB backlight | map VCC/GND/touch/R/G/B or serial/controller lines |
| 10 | mapped | buzzer (`Q17` / `BUZZER1`) | sounder / beeper | driver `Q17` (same current-driver topology + 10 ohm shunt as front IR LED drivers); control to `PA11` via RC. Next: confirm magnetic vs piezo, then drive `PA11` as timer PWM for tones |
| 11 | unknown | `U4`, `R72/R73` | battery current / voltage monitor | trace U4 outputs pin 1/pin 7 to STM32 or power logic |
| 12 | unknown | dock contacts / charge path | dock/base charge detect | find divider/comparator output that changes with dock voltage |

## Removed Assemblies / Repurpose Queue

These were original vacuum-cleaner functions. Map them before reusing the connectors for new
actuators, servos, sensors, relays, lighting, or other robot organs.

| Status | Connector | Original / suspected assembly | Repurpose notes | Next exact check |
|---|---|---|---|---|
| removed | `J18` | removed vacuum-related assembly | candidate spare connector | identify pin count, wire colors, GND/VCC, STM32/driver path |
| removed | `J16` | removed assembly / already partly tied into mixed RC signal | be careful: J16 pin 1 relates to right encoder RC path; not free until resolved | continue mapping pin 3 and any powered behavior |
| removed | `J8` | removed brush/vacuum-related assembly | candidate spare connector | identify whether it is motor drive, switch, or sensor |
| removed | `J6` | removed brush/vacuum-related assembly | candidate spare connector | identify whether it is motor drive, switch, or sensor |
| firmware-ready / mixed | `J14` | right wheel connector, not free | keep for right drive; do not repurpose | no repurpose unless wheel system changes |
| removed | `J19` | removed small vacuum / dust / pressure sensor candidate | candidate sensor connector or analog input | map to Q25/D19/R161 area and STM32 if present |

## Strict Probing Procedure For Each Connector

1. Photo / visual:
   - connector label;
   - pin count;
   - wire colors;
   - physical assembly name.

2. Power off continuity:
   - find GND pins;
   - find pins tied to `SWITCHED_SENSOR_5V`, 5 V, 3.3 V, battery/raw, or motor-driver outputs;
   - find pins that ring to STM32 pins;
   - find pins that ring to comparator/op-amp/transistor/resistor networks.

3. Record immediately:
   - add or update `docs/pinmap.csv`;
   - status stays `unknown` until both function and destination are clear.

4. Powered voltage check:
   - current-limited power;
   - measure each pin with `SWITCHED_SENSOR_5V` off;
   - enable `SWITCHED_SENSOR_5V` with firmware and measure again.

5. Stimulus test:
   - IR panel: white/black object, phone camera, IR remote;
   - bumper limit: move shell left/right/center;
   - touch/RGB: touch pad and check passive voltage changes only;
   - buzzer: trace driver transistor first; do not drive piezo/coil directly from unknown STM32 pin;
   - motor/actuator connector: do not drive until driver path is known.

6. Firmware only after mapping:
   - update `V7s_Plus.ioc`;
   - regenerate CubeMX;
   - add user-owned code module;
   - build and flash.

## Current Starting Point

Start with `J7`.

Reason: it is now identified physically as the full-width front navigation IR board, it has only
8 pins, and it probably explains a large part of obstacle/wall/dock behavior.

For `J7`, fill this mini-table first:

| J7 pin | Wire color | Off voltage | On voltage, sensor rail off | On voltage, sensor rail on | Continuity target | Behavior |
|---|---|---|---|---|---|---|
| 1 | | | | | | |
| 2 | | | | | | |
| 3 | | | | | | |
| 4 | | | | | | |
| 5 | | | | | | |
| 6 | | | | | | |
| 7 | | | | | | |
| 8 | | | | | | |
