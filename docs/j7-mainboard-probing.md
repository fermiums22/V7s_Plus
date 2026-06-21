# J7 Mainboard Probing

Use this while tracing the front bumper IR panel connector on the main board.

Photo of the removed bumper board shows this 2x4 label order near the cable:

```text
top row:    P  F  V  1
bottom row: L  R  G  2
```

## Known On Bumper Board

| J7 net | Known function on bumper board |
|---|---|
| `G` | GND, common Qx polygon |
| `V` | common feed through resistors to Q groups |
| `P` | common end of both long LED chains; mainboard side goes through R46 marked 100 to BAT+ |
| `1` | starts `D11-D10-D9-D8-D7` chain |
| `2` | starts `D6-D5-D4-D3-D2-D1` chain |
| `R` | `R -> D13/D14 -> Q1-Q4` |
| `F` | `F -> D15 -> Q5-Q7` |
| `L` | `L -> D16/D17 -> Q8-Q11` |

## Find On Main Board

| J7 net | Trace to |
|---|---|
| `G` | board GND |
| `V` | TP25, diode D7, Q26/Q27 switched 3.3 V path, STM32 `PE7` control candidate |
| `P` | R46 marked 100 to battery plus; same BAT+ path continues to J19 pin 2 |
| `1` | transistor, resistor, or STM32 |
| `2` | transistor, resistor, or STM32 |
| `R` | ADC/GPIO/comparator path |
| `F` | ADC/GPIO/comparator path |
| `L` | ADC/GPIO/comparator path |

## Report Format

```text
J7 G -> ...
J7 V -> ...
J7 P -> ...
J7 1 -> ...
J7 2 -> ...
J7 R -> ...
J7 F -> ...
J7 L -> ...
```

## Findings

```text
J7-4 V -> TP25 -> D7 -> Q26 switch
Q26 supply -> 3V3, likely P-channel/high-side
Q26 gate -> Q27
Q27 gate/control -> resistor network -> STM32 PE7
Q26 switched output is broader FRONT_IR_SENSOR_3V3: it feeds J7 V and pullups/RC signal networks for J3/J4
J7-1 label 2 -> TP28 -> Q12, marked Y1
J7-2 label 1 -> TP29 -> Q13, marked Y1
Q12/Q13: 3-contact package with large center pad to connector side; third contact through R100-marked resistor to GND; first contact goes into control/power network
Q12/Q13 may be transistor/MOSFET drivers, current regulators, or zener/reference-like parts. Do not assume gate/PWM until confirmed.
Q12 -> Q11
Q13 -> Q15
Q12 control/reference node -> resistor -> Q11-derived pullup/bias node
Q11-related bias/control node -> C34 -> STM32 PB10
Working hypothesis: Q11 forms/provides pullup/bias for control inputs of Q4/Q13/Q12, and Q11 is fed from SWITCHED_SENSOR_5V from Q20
Similar driver/control pairs:
Q4 -> Q5, goes to J4
Q13 -> Q15, goes to J7-2 LED chain
Q12 -> Q14, goes to J7-1 LED chain
Driver control/gate nodes have pullup resistors to the Q11-derived node
Q4 is not only a J4 local driver: Q4 drives a three-sensor downward cliff/floor emitter chain through J4/J3/J2:
Q4 pin 2 -> J4 pin 1 -> J4 pin 4 -> J3 pin 1 -> J3 pin 4 -> J2 pin 2 -> J2 pin 8 -> VBAT
Q4 current path uses a 3R90 shunt to GND
The separate fourth downward cliff/floor sensor has its own driver Q22:
J17 pin 2 -> Q22, Q22 current path uses 6R90 shunt to GND, J17 pin 8 -> 5V
J17 pin 8 does not ring to VBAT; this fourth downward sensor is supplied from 5V, unlike the three-sensor VBAT chain
Q12 first/control pin is tied to Q14 center pin; Q14 third pin is GND; suspected Q14 gate/control has resistor pull-down to GND, but upstream drive is not found yet
Q13/Q15 look like the same topology as Q12/Q14
J7 pins 5/6/7 (R/F/L) leave through resistor pairs on the mainboard; the upper contacts of those resistor pairs are commoned and go to the same switched power line as J7-4 V / TP25 / D7 / FRONT_IR_SENSOR_3V3
J7-5 R -> R52 -> STM32 PC3
J7-6 F -> R51 -> STM32 PA5
J7-7 L -> R50 -> STM32 PB0
J7-8 P -> R46 marked 100 -> BAT+
BAT+ path also continues to J19 pin 2
```

Open checks:

```text
PE7 low/high -> measure J7-4 V / TP25
confirm Q26 transistor type and source/drain/gate orientation
confirm Q27 transistor type and whether PE7 active-high or active-low
trace Q11 supply/input from SWITCHED_SENSOR_5V and its exact output/bias node
trace Q5/Q15/Q14 control nodes onward to STM32 or shared modulation network
identify Q12/Q13 by diode-mode readings and package/pin behavior before treating them as transistors
trace Q11 DC bias resistors around C34 before treating PB10 as direct GPIO/PWM control
trace Q14/Q15 suspected gate/control nodes upstream; they are currently pulled to GND but the active drive source is unknown
confirm Q12/Q13 package: likely SOT-89-class from pad description, but needs close-up/size
```
