# IR Emitter Chains

Working notes for the IR emitter power/current-driver topology.

## Three-Sensor Downward Cliff/Floor Chain

This chain is for three downward-looking cliff/floor IR sensor emitters. It is driven by `Q4`
and spans multiple connectors:

```text
Q4 pin 2
  -> J4 pin 1
  -> J4 pin 4
  -> J3 pin 1
  -> J3 pin 4
  -> J2 pin 2
  -> J2 pin 8
  -> VBAT
```

Current driver / sense:

```text
Q4 driver current path -> 3R90 shunt -> GND
```

Important: `Q4` is not only the local driver for `J4`; it drives a three-sensor series chain
that includes `J4`, `J3`, and `J2`.

## Separate Fourth Downward Cliff/Floor Sensor

The fourth downward-looking sensor appears to be separate and has its own current driver:

```text
J17 pin 2 -> Q22
Q22 driver current path -> 6R90 shunt -> GND
J17 pin 8 -> 5V
```

`J17 pin 8` does not ring to VBAT. It is not the same supply topology as the three-sensor chain:

```text
J2 pin 8  -> VBAT, for the three-sensor series chain
J17 pin 8 -> 5V, for the separate fourth sensor
```

Do not assume connector power by symmetry; these four downward sensors are not powered identically.
