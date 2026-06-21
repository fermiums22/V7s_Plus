# Sensor Bring-up Notes

Working target for the next probing session: identify the robot sensors, map every useful
signal to STM32 pins or analog front-end outputs, then configure only confirmed pins in
`V7s_Plus.ioc`.

The strict status tracker is `docs/bringup-checklist.md`.

## Known Starting Point

- `SWITCHED_SENSOR_5V` is a switched 5 V rail enabled by `PE5 -> Q21 -> Q20`.
- The two center/onboard IR receivers under the cover also appear to use `Q20` from the 5 V line, so this rail is likely a general switched 5 V sensor rail.
- Wheel encoders already work from this rail:
  - left wheel encoder signal: `PD3`
  - right wheel encoder signal: `PE8`
- Current ADC channels currently configured:
  - `PB1 / ADC_IN9` = left motor driver `VPROPI`
  - `PC5 / ADC_IN15` = right motor driver `VPROPI`
- These ADC channels are motor-driver current outputs, not yet battery voltage/current.
- Battery pack is 14.4 V Li-ion. Battery minus goes through low-side shunts `R72` and `R73`.
- `U4` near `R72/R73` is the best battery-current / battery-monitor candidate.
- `U10` is an 8-pin ST-marked comparator candidate, but its function is not resolved.

## User-Identified Sensor List

| Item | Connector / IC | Physical function | First hypothesis |
|---|---|---|---|
| Front black/white wheel | `J20`, related to `U10` | optical position / rotation sensor | IR reflective sensor into comparator |
| Front right IR | `J3` | right front obstacle / cliff IR | emitter + phototransistor, analog/comparator |
| Front left IR | `J4` | left front obstacle / cliff IR | emitter + phototransistor, analog/comparator |
| Front IR bumper panel | `J7`, 8 pins | full-width PCB behind IR-transparent front plastic | obstacle/dock IR optics board |
| Left side assembly | `J17` | left IR, optical bumper limit, left IR receiver | mixed sensor board |
| Right side assembly | `J2` | right IR, optical bumper limit, right IR receiver | mixed sensor board |
| Rear IR receiver | `J15` | rear IR receiver | likely demodulated digital IR receiver |
| Front onboard IR receivers | main PCB, 2 parts | front-left/front-right beacon receivers | likely dock/base direction finding |
| Touch button with RGB | `J12` | capacitive/touch key and RGB backlight | mixed user-interface board |
| Buzzer | onboard buzzer | sounder / beeper | likely transistor-driven MCU output |
| Battery | `R72/R73`, `U4` | voltage/current monitoring | low-side current sense and/or thresholds |
| Dock/base input | dock contacts, power OR path, maybe `U10` | charger/base detect | divider or comparator into STM32 |

## Probing Order

1. Power off, continuity mode:
   - find GND pin on each connector;
   - find pins tied to `SWITCHED_SENSOR_5V`, 5 V, or 3.3 V;
   - record connector pin count and wire colors.

2. Still power off:
   - for `J20`, ring every pin to `U10` pins 1-8;
   - ring `U10` pins 1 and 7 to STM32 pins or pull-up resistors;
   - ring `U10` pins 2/3/5/6 to J20, resistor dividers, dock input, or sensor rails.

3. Power on with current limit:
   - enable `SWITCHED_SENSOR_5V` through firmware (`PE5` high);
   - measure supply pins on J2/J3/J4/J7/J15/J17/J20;
   - do not drive any unknown pin from STM32 yet.

4. Signal test:
   - for J20 black/white wheel, slowly rotate the disk and watch U10 outputs pin 1/pin 7 and any STM32-side nodes;
   - for front/side IR reflectors, cover/uncover or aim at white/black surface and watch signal pins;
   - for optical bumper interrupters, block/unblock the slot and watch outputs;
   - for IR receivers, illuminate with a remote-control IR source and look for active-low pulses.

5. Only after a signal destination is confirmed:
   - add a clear row to `docs/pinmap.csv`;
   - update `V7s_Plus.ioc`;
   - regenerate CubeMX;
   - add a small sensor module in user-owned files.

## Expected Electrical Behaviors

- Demodulated IR receiver: usually 3 pins, VCC/GND/OUT, output idles high and pulses low.
- Multiple IR receivers around the perimeter are probably not obstacle sensors by themselves;
  they are a good fit for dock/base beacon direction finding.
- Reflective IR obstacle/cliff sensor: may be analog phototransistor or comparator output.
- Optical interrupter / bumper limit: usually digital, toggles when the flag blocks the slot.
- Black/white wheel sensor: likely reflective IR; raw analog signal may be cleaned by `U10`.
- LM293/LM393-style comparator outputs are open collector, so outputs need pull-ups and often read as active-low.

## Front Bumper / IR Panel Working Hypothesis

There are probably two separate "bumper" concepts:

- mechanical impact bumper: the plastic front shell moves when it hits an obstacle;
- optical front panel: `J7`, an 8-pin full-width PCB behind the IR-transparent front plastic.

The `J7` board is probably not the impact switch itself. With IR-transparent plastic and an
8-pin harness, it is more likely a front optical board used for obstacle reflection, wall/front
sensing, and/or dock beacon reception.

Observed on the removed board:

- components are labeled `Dxx` and `Qxx` in an alternating/staggered pattern;
- far-right `D11` was covered by foam tape, likely an optical mask or factory tuning detail.
- all `Qxx` parts have one common contact on a shared GND polygon, and this net goes to `J7-3`.
- J7 connector markings: pin 1 = `2`, pin 2 = `1`, pin 3 = `G`, pin 4 = `V`, pin 5 = `R`, pin 6 = `F`, pin 7 = `L`, pin 8 = `P`.
- Q groups found so far: `Q1/Q2`, `Q3/Q4`, `Q5/Q6/Q7`, `Q8/Q9`, `Q10/Q11`. By visual/probing they may simply be LEDs; exact part type remains TBD.
- Group resistors are commoned to `V`; each resistor's other side goes to its Q-group node.
- `D13-D17` cathodes connect to those same Q-group nodes.
- `R -> D13 -> Q1/Q2` and `R -> D14 -> Q3/Q4`.
- `F -> D15 -> Q5/Q6/Q7`.
- `L -> D16 -> Q8/Q9` and `L -> D17 -> Q10/Q11`.
- J7 pin 1, label `2`, goes to `D6`; `D6-D5-D4-D3-D2-D1` are connected in series; after `D1` the trace returns to J7 pin 8, label `P`.
- J7 pin 2, label `1`, goes to `D11`; `D11-D10-D9-D8-D7` are connected in series; after `D7` the trace also returns to J7 pin 8, label `P`.
- Physical/optical Q/D order: `Q2-D1`, `D2-Q1`, `Q3-D3`, `D4-Q4`, `Q7-D5`, `D6-Q6`, `Q5-D7`, `D8-Q9`, `Q8-D9`, `D10-Q10`, `Q11-D11`.

Likely 8-pin breakdown candidates:

- shared VCC;
- shared GND;
- one or more IR LED drive lines;
- two or more analog/digital receiver outputs;
- possibly left/right/center zones, or separate emit/receive groups.

Do not assume all 8 pins go directly to STM32. Some may go through transistor drivers,
comparators, or resistor networks.

Practical checks for `J7`:

1. Power off, find GND and VCC pins by continuity to board rails.
2. Look for pins that ring to resistors/transistors near J7; those are likely IR LED drive pins.
3. Look for pins that ring to comparator/op-amp inputs or STM32 inputs; those are likely receiver outputs.
4. Powered, use a phone camera to see whether any IR LEDs light when firmware toggles candidate drive lines.
5. With the board powered but before driving unknowns, check which pins change when a white object moves in front of the bumper.

## Mechanical Impact Bumper Hypothesis

The plastic front shell still likely acts as a floating mechanical ring/arc. Small tabs or flags
inside the bumper may interrupt left/right optical slots or press limit mechanisms on the side
assemblies.

Most likely useful signals:

- left bumper event from the left side assembly `J17`;
- right bumper event from the right side assembly `J2`;
- possibly a shared/front bumper harness through `J7`.

If the side assemblies really contain optical bumper limits, expect the bumper to work like this:

1. Normal state: optical path is open or blocked in a stable resting position.
2. Impact from left/front/right shifts the bumper shell.
3. A plastic flag changes one or both optical interrupters.
4. Firmware derives hit direction from left/right state combination:
   - left only = left hit;
   - right only = right hit;
   - both = front/center hit.

Because `J7` is now identified as the full front IR panel, the mechanical impact state may be
mostly on `J17` and `J2`. Confirm by moving the bumper by hand while watching voltage on the
left/right optical limit outputs.

## Battery And Dock Checks

- Battery current:
  - start at `R72/R73` shunts;
  - trace both shunt sides into `U4`;
  - trace `U4` pin 1 and pin 7 to STM32 ADC pins or protection logic.

- Battery voltage:
  - look for a high-value divider from battery plus/raw rail to STM32 ADC or comparator input;
  - never connect raw battery directly to STM32 ADC.

- Dock/base detect:
  - dock contacts feed a diode bridge / OR input path before `U7`;
  - find any divider/comparator output that changes when dock voltage is present;
  - if `U10` inputs trace to dock/power instead of `J20`, classify U10 as dock/charge detect rather than front wheel sensor.

## Buzzer Checks

- Identify whether the buzzer is magnetic or piezo.
- Find one side tied to VCC or GND.
- Trace the other side to a transistor/resistor network, then to STM32.
- If it is transistor-driven, firmware should drive only the transistor base/gate.
- If it is passive piezo, use a timer/PWM-capable pin for tones.
- If it is active buzzer, a simple GPIO on/off may be enough.

## Firmware Connection Rule

Do not assign GPIO/ADC names in `V7s_Plus.ioc` from connector names alone. The pin must be
confirmed at the STM32 side or at a confirmed comparator/op-amp output first.
