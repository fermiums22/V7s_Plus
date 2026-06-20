# Power Path Notes

Working notes from probing on 2026-06-20.

## Confirmed Rails

```text
raw / 24 V-ish rail -> U7 TPS54331-like buck -> 5 V rail -> U8 -> 3.3 V rail
```

- `U7` is confirmed as the 5 V generator.
- `U8` is confirmed as the 5 V to 3.3 V regulator.

## Motor Driver Power

- With battery connected, power appears on the wheel motor driver supply capacitors.
- Battery power feeds the wheel motor drivers and also reaches `Q8`.
- `Q8` is `FR9024N` / IRFR9024N-family P-channel MOSFET.
- `Q18` is another FR9024-family P-channel MOSFET.
- Exact `Q8` / `Q18` source-drain-gate roles still need continuity mapping.

## Main Power Switch

- Main power is enabled through the power switch in connector `J1`.
- `J1` also appears related to a reserve / external 24 V input path.

## Input OR Path For U7

Diodes `D18`, `D16`, `D9`, and `D10` appear to form an OR-ing input network so `U7` can be powered from multiple sources:

- `J10`: 2-pin dock station input.
- `J1`: reserve / external 24 V input.
- Battery path through the same `J1` area when the power switch is closed.

Treat this as a working schematic until diode orientation and exact nodes are confirmed.

## Encoder / Sensor Common Power Net

- `J5-7`, `J14` encoder/sensor VCC, `TP11`, `TP46`, `TP58`, and `TP71` ring together.
- This is the common encoder/sensor VCC candidate net: `ENC_VCC_COMMON`.
- Its actual power source/switch is not yet confirmed.
- `Q24` may be related to this area, but no clear Q24 voltage/function has been proven yet.
