# J5 Pin 7 Trace Notes

Target net: `J5-7`, orange wire, shared with the equivalent `J14` encoder/sensor VCC wire.

Photo set: `docs/pcb_photo`, added 2026-06-20.

## Visual Trace From Photos

`J5` appears to be the lower-left long wheel connector on the top side, near:

- `U2` motor driver
- `R31` marked `R100`
- `D19`
- `Q24/Q25` nearby power/signal switching area

The pin-7 trace is not fully exposed in one continuous visible run. From the photos it appears to:

1. Leave `J5` on the top side as a thin/signal-power trace, not as one of the two heavy motor traces.
2. Go to a nearby via / inner or bottom-side routing near the lower-left connector group.
3. Continue on the bottom side along the left edge as part of the vertical bundle of traces near test pads:
   - `TP10`
   - `TP7`
   - `TP6`
   - nearby `TP31/TP30/TP8`
   - confirmed related checkpoint: `TP58`
4. Return toward the top-side area shared with `J14` encoder/sensor power.
5. Belong to the common `J5/J14` encoder/sensor VCC net.

This matches earlier continuity findings:

- `J5-7 = orange = encoder/sensor power candidate`
- `J14` and `J5` encoder/sensor VCC wires ring together
- `TP11`, `TP46`, `TP58`, and `TP71` ring to this common `J5/J14` net
- `Q24` is still only a working theory for switching/controlling this area; no clear voltage has been observed on it yet
- `Q24` gate/control-like path goes through `R146` to STM32 `PA8`, but Q24 function is not confirmed

## Confidence

Visual confidence: medium.

Electrical confidence: high that `J5-7`, `J14` encoder/sensor VCC, and `TP58` are the same common net.

Functional confidence: low/medium that this is a Q24-switched rail until voltage and Q24 source/drain/gate are confirmed.

Confirmed continuity:

- `J5-7` rings to `TP58`.
- `J5-7` rings to `J14` encoder/sensor VCC wire.
- `TP11`, `TP46`, `TP58`, and `TP71` ring together and are part of the same `SWITCHED_SENSOR_5V` net.
- `Q20` is connected between 5V and `SWITCHED_SENSOR_5V` and sits closer to the power-supply blocks; this is now the likely source/switch for the rail. The control path is still not traced.
- `Q21` is tied to the Q20 gate/control node through a resistor and to GND, likely acting as the Q20 gate pull-down enable transistor. Q21 gate/base is the next control path to trace.
- STM32 `PE5` drives Q21 gate/base. Firmware label: `SWITCHED_SENSOR_5V_EN`; PE5 high should enable Q20 and raise `SWITCHED_SENSOR_5V`.
- Live test confirmed: firmware drove PE5 high and 5V appeared on `SWITCHED_SENSOR_5V`.

Other exact bottom-side test pad identities still need continuity confirmation because the photos do not show a complete unbroken copper path from `J5-7` to every nearby pad.

## Fast Continuity Checks

With power disconnected:

1. `J5-7` to `J14` orange / encoder VCC pin: should ring.
2. `J5-7` to `J14` encoder/sensor VCC: confirmed common net.
3. `J5-7` to `TP58`: confirmed ring.
4. `J5-7` to `TP10`, `TP7`, `TP6`, `TP31`, `TP30`, `TP8`: optional, check which of these rings; record exact pad.
5. `J5-7` to each Q24 pin: record actual continuity; do not assume Q24 is the switch yet.
6. `J5-7` to GND: should not be shorted. If it rings low-ohm to GND, stop and re-check pin numbering.

## Likely Functional Name

Keep using:

```text
SWITCHED_SENSOR_5V
```

or, if later confirmed switched:

```text
ENC_VCC_SW
```
