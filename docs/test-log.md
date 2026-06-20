# Test log

## 2026-06-20

- Project generated for STM32F071V8Tx.
- Original firmware is gone after readout protection unlock. We will write a minimal firmware.
- Current firmware state: only clock/SWD generated, no application pins assigned yet.
- Next required hardware data: connector pinout, motor driver markings, 3.3 V/5 V rail measurements.
- Wheel motors have 5 wires each.
- Left wheel connector: J5. Colors observed: red, black, yellow, gray, orange.
- Right wheel connector: J14. Same apparent wire colors.
- Left driver near J5 is U2, marking looks like DRV8801. Right driver near J14 is U6.
- R31 and R108 are marked R100, likely 0.100 ohm current-sense shunts.
- Left gray wire rings to board negative / R31 area, so it is likely encoder GND.
- Red and black wires are confirmed as the wheel motor winding pair.
- Left J5 pinout so far: J5-1 black MOTOR_B, J5-2 red MOTOR_A, J5-5 yellow encoder signal, J5-6 gray GND, J5-7 orange probable encoder power.
- No voltage found on J5-7/orange yet. Encoder/sensor power may be switched by STM32, a load switch/regulator, or another power-state condition.
- J5-7/orange, J14 encoder/sensor VCC, and TP58 ring together. Treat as common net `ENC_VCC_COMMON`.
- Q24 remains only a working theory for this area. It may be related, but no clear voltage has been observed on Q24 yet, so do not assume it is the encoder power switch.
- Q24 control-like node traces through R146 to STM32 PA8, configured in CubeMX as label Q24.
- R147 is connected between the suspected + rail and Q24 gate/control-like node. This suggests high-side style biasing, but function is not confirmed.
- V7s_Plus.ioc was updated so PA8/Q24 is generated as open-drain and default high/off. Code was regenerated through STM32CubeMX.
- Nearby Q25 appears to be another node: one pin goes to D19 and one contact of J19, another path goes through R161 marked R100 to GND. Treat as a separate circuit until proven otherwise.
- Powered test-point snapshot: TP3/TP24/TP57 = 24 V; TP49/TP48/TP55 = 5 V; TP74/TP34/TP56/TP37/TP39/TP38/TP35/TP36 = 3.3 V; TP1/TP2 = 0 V power-switch nodes; other checked testpoints = 0 V. See `docs/testpoints.csv`.
- TP11, TP46, TP58, and TP71 ring together with J5/J14 encoder/sensor VCC. Treat as `ENC_VCC_COMMON`.
- With battery connected, motor driver supply capacitors get power. Battery feeds the wheel motor drivers and Q8.
- Power is enabled through the power switch in connector J1.
- D18/D16/D9/D10 appear to form a diode-OR input network feeding U7 from J10 dock input, J1 reserve/external 24 V input, or battery through J1 when the switch is closed. See `docs/power-path.md`.
