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

## Live probing log (2026-06-24, user ring-out)

Notation: `R` = right pad of a resistor, `L` = left pad (as the user views the board).

Physical cluster between the shunts and U4 (top -> bottom):
```
R73
R72
R74  R69
R75  R70
R76  R68
C42
U4
```
Left column = R74 / R75 / R76. Right column = R69 / R70 / R68.

Continuity found so far:
- `R72:R -> R74`  (shunt R72 right pad goes into R74)
- `R69:L -> GND`  (same GND node R72 sits on = shunt low side)
- `U4:2 (IN1-) -> R68:R`
- `U4:3 (IN1+) -> cluster C38 / R64 / R65 / C37`, and it is a **biased reference**:
  - `R63` pulls U4:3 up to **5V**
  - `R65` pulls U4:3 down to **GND**
  - i.e. U4:3 = resistor divider 5V/GND, RC-filtered (C37/C38) = a fixed reference voltage.

### Interpretation (refines "low-side current-sense op-amp")
This matches a **single-supply op-amp current-sense amp on channel A1**:
- `pin3 (IN1+)` held at a filtered **reference** (R63/R65 divider off 5V) - the mid-point the
  amplified shunt signal swings around (needed because a single-supply op-amp can't go below 0V).
- `pin2 (IN1-)` is the summing/feedback node via `R68` (gain set by R68 + a feedback R).
- Shunt voltage enters the network through `R74` (from R72 hot side).
- `pin1 (OUT1)` should then be the amplified battery-current analog -> trace to an STM32 ADC.

### Highest-value next continuity checks
1. **`U4:1` (OUT1) -> ?** most important: does it reach an STM32 ADC pin (battery current) or a divider?
2. **`R68:L` far end and `R74` far end** - do they meet at the shunt-hot node, or does R68 tie back
   to `U4:1` (that would prove the feedback resistor / inverting-amp config)?
3. **`U4:5 / :6 / :7`** (second op-amp A2) - to a battery+ divider (=voltage monitor) or a 2nd shunt tap?
4. Confirm `R75 / R76 / R70` roles (likely the rest of the gain/divider network for A1/A2).

Update: **`U4:1` is shorted directly to `U4:2`** (zero ohms, no feedback resistor).
=> A1 is a **unity-gain voltage follower (buffer)**, NOT the current amplifier.

### Revised interpretation: A1 = reference buffer, A2 = the current amp
- **A1 (pins 1/2/3)** buffers the R63/R65 (5V-derived, RC-filtered) **reference**: pin3 in,
  pin1=pin2 out = low-impedance copy of that reference, fed out through `R68`.
- This buffered reference is the **offset/bias** the actual shunt amplifier swings around.
- So the **current measurement is on A2 (pins 5/6/7)** - that is now the pin pair to chase.

### Next checks (now focused on A2)
1. **`R68` far end** - where does the buffered reference land? (expect: A2's input network, pin5 or pin6).
2. **`U4:6 (IN2-)` and `U4:5 (IN2+)`** - one should tie to the **shunt** (through R74/R75/R76 from the
   R72/R73 hot side), the other to the buffered reference / feedback. That pair = the current sense.
3. **`U4:7 (OUT2)` -> ?** THE battery-current analog output - trace to an STM32 ADC pin.
4. Gain = set by the R network around A2 (R70/R75/R76...) once both inputs + feedback are known.

Update: **`U4:7` (OUT2) -> `C42:L` and `R76:L`** (output node ties to cap C42 + resistor R76).
- `C42` on OUT2 = filter cap (if its other pad is GND -> RC low-pass on the current signal;
  if it sits across R76 -> feedback/stability cap).
- `R76` off OUT2 is the key one: tracing `R76:R` ->
  - if `R76:R -> U4:6 (IN2-)` => R76 = **feedback resistor of A2** (sets the gain). [strong: confirms current amp]
  - if `R76:R -> STM32 pin`   => R76 = **output series R to the ADC** (then OUT2 = battery-current to MCU).
- (R76 is in the left column R74/R75/R76, next to C42 just above U4 - consistent with the A2 output stage.)

Update: **`R76:R -> U4:6 (IN2-)`** => `R76` = **feedback resistor of A2**. CONFIRMED:
A2 (pins 5/6/7) is the current-sense amplifier (inverting/difference), gain = `R76 / R_in`.
C42 (also on pin7) is most likely the feedback cap across R76 = RC low-pass on the current signal.

So the U4 picture is now:
- **A1 (1/2/3)** = unity buffer of the R63/R65 (5V) reference -> low-Z bias via R68.
- **A2 (5/6/7)** = shunt current amp; `pin6 (IN2-)` summing node, `R76` feedback, `pin7 (OUT2)` = current out.

### Remaining to confirm
1. **`U4:6` input resistor** (the OTHER R on pin6 besides R76) -> to the shunt (R74/R75 from R72/R73 hot)
   or to the buffered reference? That sets which way the amp reads + the gain denominator. (likely R70 or R75)
2. **`U4:5 (IN2+)`** -> buffered reference from R68? or a shunt tap? (sets the offset/common-mode)
3. **Does the `pin7`/OUT2 node reach an STM32 ADC pin?** <-- the whole point; not yet traced to the MCU.
4. Read the markings/values of R76 + the pin6 input R -> compute the gain (V_out per amp of battery current).

Update: **`U4:7` (OUT2) -> `R71` -> STM32 `PA7`**. PA7 = **ADC_IN7**, so the battery-current
analog goes to STM32 ADC channel 7 (R71 = series input resistor to the ADC pin). CHAIN COMPLETE:
`battery- -> shunt R72||R73 -> A2 amp (U4) -> R71 -> PA7 / ADC_IN7`.

=> U4 is confirmed a **battery low-side current-sense front-end** (dual op-amp: A1 buffers a 5V
reference, A2 amplifies the shunt). Function class settled; exact ST part still unidentified but no
longer needed for firmware. PA7 is the pin to add as an ADC channel when we wire battery-current read.

### Still open (nice-to-have, not blocking)
- `U4:6` input resistor value + `R76` value -> gain (V/A). With R72||R73 shunt value -> amps per ADC count.
- `U4:5 (IN2+)` source (buffered ref vs shunt tap) - sets the zero-current offset on PA7.
- `U4:1`/A2 second-stage exact role of R74/R75/R70 in the divider chain.

(log continues)

## Useful References

- ST TSC101 current-sense amplifier: https://www.st.com/resource/en/datasheet/tsc101.pdf
- ST TS3702 dual comparator: https://www.st.com/resource/en/datasheet/ts3702.pdf
- ST LM358 family dual op-amp datasheet: https://www.st.com/resource/en/datasheet/lm158.pdf
