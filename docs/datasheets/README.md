# Datasheets

Downloaded datasheets for identified or high-confidence ICs. Do not treat fuzzy top markings as confirmed parts until continuity and package/pinout match.

## Downloaded

- `U2_U6_TI_DRV8801_DRV880x_motor_driver.pdf`
  - Source: `https://www.ti.com/lit/ds/symlink/drv8801.pdf`
  - Board candidates: `U2` near `J5`, `U6` near `J14`
  - Confidence: probable; board marking looked like DRV8801-like motor driver.

- `U7_TI_TPS54331_24V_to_5V_buck.pdf`
  - Source: `https://www.ti.com/lit/ds/symlink/tps54331.pdf`
  - Board candidate: `U7` marked `54331`, SOT-8/SO-8 area with diode and inductor
  - Confidence: confirmed as 5 V generator; close photo shows `54331` with TI-like marking and the surrounding `D17`/inductor topology matches a non-synchronous buck converter.

- `Q8_Q18_Infineon_IRFR9024N_P_channel_MOSFET.pdf`
  - Source: `https://www.infineon.com/dgdl/irfr9024npbf.pdf`
  - Board candidates: `Q8` marked `FR9024N`, `Q18` marked `FR9024`
  - Confidence: confirmed markings; likely P-channel high-side power switches, exact board nets TBD.

- `U3_U4_TI_LM158_LM258_LM358_dual_op_amp_alt.pdf`
  - Source: `https://www.ti.com/lit/ds/symlink/lm158-n.pdf`
  - Board candidate: `U3` marked `258 G9929`, on the `J8` main-vacuum-pump shunt path; likely reference for `U4` as well
  - Confidence: probable family match. ST direct datasheet download timed out, so TI LM158/LM258/LM358-family datasheet is kept as alternate pinout/behavior reference. U3 is powered from `SWITCHED_SENSOR_5V`, likely pin 8 if the LM258 pinout matches; its exact role on the pump-shunt path is not assigned.

- `U10_ST_LM193_LM293_LM393_dual_comparator.pdf`
  - Source: ST `LM193/LM293/LM393` datasheet, downloaded from Mouser mirror after direct ST download timed out: `https://www.mouser.com/datasheet/2/389/CD00000465-105244.pdf`
  - Board candidate: `U10` near dock/power area, user read ST logo and `293` top marking
  - Confidence: probable. ST datasheet lists SO-8 `LM293D/LM293DT` marking as `293`; pin 8 rings to `SWITCHED_SENSOR_5V`; still confirm pin 4 GND and outputs pin 1 / pin 7 on the board.

- `TCH223_TTP223-BA6_Tontek_touch_controller.pdf`
  - Source: `https://www.tontek.com.tw/uploads/product/243/TTP223-BA6_V2.1_EN.pdf`
  - Board candidate: SOT-23-6 touch-controller on the RGB touch-button PCB, marked by Victor as `TCH223` / `BC2035`
  - Confidence: probable compatible/re-marked `TTP223-BA6` or `TTP223N-BA6`, not exact-order-code confirmed. The package, single `PAD1` electrode, and sensitivity network fit the one-key capacitive controller. Confirm package orientation, VSS, VDD, and Q by continuity before using its pinout.

## Still Need Identification

- `U4`: 8-pin IC near battery low-side shunts `R72` and `R73`; ST logo, marking possibly like `G9884`, but no reliable top-mark match found yet. U3 similarity and pinout make LM258/LM358-family likely. Candidate functions are documented in `../u4-candidates.md`.
- `U8`: marked `99M7ERE`; behavior confirmed as 5 V input / 3.3 V output regulator, exact IC not identified.
- `U10`: likely ST `LM293` / `LM393` family dual comparator after user clarified top marking as `293`. Pin 8 is on `SWITCHED_SENSOR_5V`; exact function in dock/power / encoder-sensor enable path is still TBD by continuity and voltage checks.

See `../components.csv` for full board component inventory.
