# U4 Candidates

`U4` is an 8-pin IC near the battery low-side current shunts `R72` and `R73`.
The package has an ST logo, but the top marking is not readable enough to identify the exact part.
User readout looked like `G9884` / similar, but searches did not find a reliable ST top-mark match.

## Board Context

- Battery pack: 14.4 V, 2400 mAh Li-ion.
- Battery minus goes through parallel shunts `R72` and `R73` to board GND.
- `U4` sits next to those shunts.
- `U4 pin 4` is confirmed GND.
- `U4 pin 8` is VCC and is fed from the `U8` 3.3 V output.
- `U8` has 5 V input and 3.3 V output.
- `U3` is marked `258 G9929` and looks similar in marking style to `U4`. This strongly suggests `U3` is an LM258-family dual op-amp and raises the probability that `U4` is a related ST dual op-amp.
- Nearby parts include several RC / resistor pads, suggesting analog signal conditioning.
- Most likely purpose: battery current measurement or battery/protection threshold detection.

## Most Likely Options

### 1. Dual op-amp used as low-side current sense amplifier

Examples: ST `LM358`, `LM258`, similar dual op-amps.

Why plausible:

- SO-8 package is common.
- Low-side shunt sensing often uses an op-amp plus external resistors.
- The surrounding resistor network fits a discrete gain/filter circuit.
- Output can go to STM32 ADC for battery current.
- Nearby `U3` marked `258` appears to be the same/similar ST dual op-amp family.

Quick checks:

- Pin 4 is confirmed GND.
- Pin 8 is 3.3 V logic/analog supply from `U8`.
- Pins 2/3 or 5/6 should trace through resistors to the two sides of `R72/R73`.
- Pin 1 or pin 7 likely traces to STM32 ADC or another analog/protection node.

Confidence: very high as a function class after `U3 = 258 G9929` observation, exact U4 part/top-mark still unknown. Confirmed pins 4/8 match this pinout.

### 2. Dual comparator for overcurrent / battery state thresholds

Examples: ST `TS3702`, LM393-compatible dual comparator family.

Why plausible:

- SO-8 package is common.
- Battery and current protection circuits often need threshold detection, not precise current measurement.
- If outputs go to STM32 GPIO or transistor gates instead of ADC pins, comparator becomes more likely.

Quick checks:

- Pin 4 is confirmed GND, pin 8 is probable supply.
- One input pair should see shunt-derived voltage.
- Other input pair should see a resistor divider/reference.
- Outputs may have pullups and trace to STM32 digital input, charger logic, or shutdown transistor.

Confidence: medium. Confirmed pins 4/8 also match LM393/TS3702-style dual comparator pinout, but similar `U3` marking with `258` now favors dual op-amp.

### 3. Dedicated current-sense amplifier / current monitor

Examples: ST current-sense families such as `TSCxxx`.

Why plausible:

- ST makes current-sense amplifiers for shunt measurement.
- The board location screams current measurement.

Why less likely:

- Many ST current-sense parts use SOT23-5, MiniSO8, or specific pinouts; exact match must be proved.
- The known ST `TSC101` is high-side-oriented, while this board has a low-side battery shunt.
- Confirmed `pin 4 = GND` and probable `pin 8 = VCC` point more toward classic dual op-amp/comparator pinouts than many dedicated monitors.

Quick checks:

- Look for two closely matched input pins going directly/Kelvin through resistors to the shunt.
- Look for one analog output to STM32 ADC.
- Compare measured pinout against candidate datasheet before assuming exact part.

Confidence: low for exact dedicated current-sense IC, high for current-monitor role.

### 4. Battery voltage / charger analog front-end

Why plausible:

- Same area also belongs to battery and dock/charge power path.
- It may combine current sense, battery voltage threshold, charge detect, or fault latch logic.

Quick checks:

- If inputs trace mostly to battery plus dividers and charger nodes, not to both shunt sides, this rises in likelihood.
- If outputs drive charger enable/protection transistors, this may be a supervisory analog block rather than ADC current sense.

Confidence: low-medium.

## Next Probing Plan

With board unpowered:

1. Find which `U4` pin is GND by continuity to board GND.
2. Find which `U4` pin has continuity/resistance to the logic rail capacitors, likely VCC.
3. Trace both sides of `R72/R73` to `U4` pins through nearby resistors.
4. Trace `U4` outputs to STM32 pins or to transistor/charger logic.

Known so far:

- `U4 pin 4 = GND`
- `U4 pin 8 = 3.3 V VCC`, fed from `U8` output
- `U3 = 258 G9929`, likely LM258-family dual op-amp

With current-limited power:

1. Measure `U4` supply voltage.
2. Measure outputs at idle.
3. Apply a small known load and watch whether an output changes linearly. Linear means op-amp/current monitor; switching threshold means comparator.

## Useful References

- ST TSC101 current-sense amplifier: https://www.st.com/resource/en/datasheet/tsc101.pdf
- ST TS3702 dual comparator: https://www.st.com/resource/en/datasheet/ts3702.pdf
- ST LM358 family dual op-amp datasheet: https://www.st.com/resource/en/datasheet/lm158.pdf
