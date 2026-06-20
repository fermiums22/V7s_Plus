# V7s Plus bring-up

Goal for the first session: safely identify power, motor driver control pins, encoders,
bumper switches, and cliff sensors, then spin the wheel motors at low duty cycle.

## Safety setup

- Keep the robot on a stand with wheels in the air.
- Use a current-limited bench supply if possible.
- Start with low current limit: 200-500 mA for logic probing, then raise only when testing motors.
- Do not connect STM32 GPIO directly to motor wires.
- Disconnect battery/charger when using continuity mode.
- Share ground between ST-Link, board logic, and any external signal source.

## Bring-up order

1. Identify GND points.
2. Identify battery input and switched/unswitched rails.
3. Power the board from current-limited supply and measure 3.3 V / 5 V rails.
4. Label every connector by physical position and wire colors.
5. Trace wheel motor connectors:
   - two thick/low-resistance wires: motor winding
   - remaining wires: encoder VCC/GND/A/B or tach output
6. Find motor driver ICs and trace their STM32-side control pins.
7. Configure only confirmed pins in CubeMX / code.
8. Test one motor driver with very low PWM duty cycle.
9. Add encoder reading.
10. Repeat for the second wheel.

## Battery / charge input notes

- Battery pack observed: 14.4 V, 2400 mAh, Li-ion.
- Battery minus goes through two parallel shunts `R72` and `R73` to board GND.
- Battery plus goes to `D8`; observed connection is cathode to GND, so treat it as a protection/clamp candidate until package/orientation is verified.
- `U4` is an 8-pin IC near the battery low-side shunts. It has an ST logo; top marking is very unclear, possibly something like `G9884`. Likely related to current sensing or battery monitoring, but keep it unknown until traced.

## First motor test policy

- Direction pins must have known idle states before enabling PWM.
- PWM starts at 0%.
- First pulse: 5-10% duty for 200 ms, then back to 0%.
- Increase duty only if current draw is sane and motor turns freely.
- Stop immediately on heating, smell, clicking relay-like behavior, or current jump.

## Pin naming convention

Use names like:

- `MOTOR_L_PWM`
- `MOTOR_L_IN1`
- `MOTOR_L_IN2`
- `MOTOR_L_EN`
- `ENC_L_A`
- `ENC_L_B`
- `BUMPER_L`
- `CLIFF_FL`

Record every discovery in `docs/pinmap.csv`.
