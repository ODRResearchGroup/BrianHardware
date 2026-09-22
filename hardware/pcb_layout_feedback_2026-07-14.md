# BRIAN 2.0 — PCB Layout Feedback (2026-07-14)

**Date:** 2026-07-14
**Source:** Design review feedback on the preliminary PCB (collaborator + David Kadish)
**Relates to:** [V2_design.md](V2_design.md), [pwm_driver.md](pwm_driver.md), [heater_power.md](heater_power.md), [sensor_output_architecture.md](sensor_output_architecture.md), [schematic_review_2026-07-03.md](schematic_review_2026-07-03.md)
**Status:** Decisions + one open question from a layout/component review of the preliminary board. Not yet drawn in the schematic or laid out on the PCB.

## Latest schematic revision

The schematic reviewed in [schematic_review_2026-07-03.md](schematic_review_2026-07-03.md) was the **2026-07-03** export (26G3, 26 sheets). A newer export dated **2026-07-07** (26 sheets, same hierarchical structure — host/PWM co-processor split, TBD62083APG drivers, ADS131M08 readout) has since been shared and is the current baseline for this feedback. It has been added to the shared schematic folder as `26G7 - iiitb - project_eNose.pdf`, following the existing `26<month-letter><day>` naming convention (E = May, F = June, G = July). None of the blockers/fixes flagged in the 2026-07-03 review (ADC CLKIN, VBUS/VIN, BQ27441 sense node, etc.) have been re-verified against 26G7 yet — treat that checklist as still open until confirmed against this revision.

---

## 1. Component changes

### 1.1 Buffer op-amp: TLV2372 → TLV9152 (SOT-23-THN)

**Decision:** Replace the per-sensor unity-buffer op-amp with **TI TLV9152** (`TLV9152IDDFR`), in the **SOT-23-THN (DDF)** package, 8-pin.

- Same RRIO CMOS dual-op-amp class as the currently-specified TLV2372/OPA2334 (rail-to-rail output, low offset — TLV9152 datasheet lists ±125 µV typ. offset, ±0.3 µV/°C drift, 4.5 MHz bandwidth), so it should drop into the existing "buffer op-amp" role in [sensor_output_architecture.md](sensor_output_architecture.md) without changing the surrounding RC/gain-config design.
- **Verify before layout:** input bias current and offset against the design's stated requirements (`Ib < 10 pA`, `Vos < 1 mV`) using the TLV9152 datasheet directly — the numbers above are close but should be confirmed against the actual spec table, not this summary.
- **Footprint change:** SOT-23-THN is a smaller, tighter-pitch 8-pin package than the SOT-23-8 dual package currently drawn on all 14 sensor sheets. This is a single shared symbol/footprint in the schematic, but the PCB footprint library needs a SOT-23-THN (DDF) footprint added/verified — reflow-solderable but tighter tolerance than the outgoing part. Confirm the fab/assembly house's SMT process can hit the pitch before committing.
- Datasheet: [ti.com/product/TLV9152](https://www.ti.com/product/TLV9152)

### 1.2 DMOS driver package: PDIP → SSOP (TBD62083A)

**Decision:** Use the **SSOP** package variant of the Toshiba TBD62083A 8-channel DMOS driver — part number **TBD62083AFNG** (`SSOP18-P-225-0.65`) — in place of the through-hole **TBD62083APG** (`P-DIP18-300-2.54-001`) currently specified in [pwm_driver.md](pwm_driver.md) and drawn in the schematic.

- The current part is literally a through-hole DIP package, which is inconsistent with an otherwise reflow-assembled board (see op-amp package note above). SSOP is surface-mount and reflow-solderable.
- Electricals (channel count, R_DS(on)/voltage-drop figures, COM-pin-floating guidance) are unchanged between PG and FNG variants — only the package/footprint and part suffix change.
- Two devices are used (14 of 16 channels); both need the new footprint.
- Datasheet: [Toshiba TBD62083A series](https://toshiba.semicon-storage.com/info/datasheet_en_20160511.pdf?did=29893)

---

## 2. Battery connector

**Decision:** Primary battery connector is **JST-PH (2-pin)**, matching the LiPo/Li-Ion packs already referenced in the root [README.md](../README.md) BOM. In addition, lay down **alternate footprints in the same location** for a 2-pin screw terminal and a standard 0.1" (2.54 mm) header, so the connector can be chosen at assembly time without a respin.

- This is a **populate-one-of-three** provision: JST-PH, screw terminal, and header footprints should share the same BatP/GND pads (or be placed close enough that only one is stuffed), not three separate parallel connectors.
- Matches the project's existing "reconfigurable prototype" philosophy already used elsewhere in the design (buffer gain-config jumpers, L2 post-filter 0 Ω link option in [heater_power.md](heater_power.md)).
- No change to the charger/fuel-gauge circuit (BQ24075/BQ27441) — this is a footprint/BOM-option decision only, not a change to BatP/VSYS/VIN topology (see [schematic_review_2026-07-03.md](schematic_review_2026-07-03.md) §2 for those nets).

---

## 3. User buttons

**Decision:** Prefer **90° (right-angle) SMD tactile switches mounted on the board edge** for the user-facing buttons, similar in form factor to [this RS Components part](https://au.rs-online.com/web/p/tactile-switches/7931683) (exact part not fixed — any right-angle edge-mount SMD tactile switch is acceptable).

- Prefer all buttons on **one edge/side** of the board for consistent access once enclosed.
- Exception: the RP2040's own switch (SW3, on RUN) can stay wherever it makes sense on the board — it doesn't need to be edge-mounted with the rest if routing/placement favours keeping it near the RP2040.
- This affects the ESP32-S3 buttons currently on the schematic: **SW1 (GPIO0/BOOT)**, **SW2 (EN/RESET)**, and the **new third user button** called for in [schematic_review_2026-07-03.md](schematic_review_2026-07-03.md) §2(3). All three should move to edge-mount SMD parts and be grouped on one side.
- Doesn't change the circuit-correctness items already flagged in the schematic review (§2(4): active-low, pull-up to 3V3, no series R, cap to GND) — those corrections still apply regardless of switch package.

---

## 4. General floorplan guidance

New, board-level placement guidance (not yet reflected in any existing doc as a floorplan — [heater_power.md](heater_power.md) and [pwm_driver.md](pwm_driver.md) currently only give circuit-local layout rules, not a whole-board zoning plan):

- **Power electronics near the board edge**, for heat dissipation — this covers the BQ24075 charger, TPS63001 buck-boost (3.3 V), and the two MPM3805 heater bucks (1.8 V / 2.5 V).
- **Keep the rear (bottom) side of the board empty where possible.** Non-solderable components (e.g. jumper wire links, like the buffer gain-config jumpers or the L2 0 Ω link option) can go on the rear to save front-side space, since they don't need reflow.
- **Align the buck converters with the DMOS driver arrays** they feed — shortens and simplifies the heater power path (buck → filter → DMOS driver → heater), complementing the existing "minimise the high-current switching loop" and short-SW-node guidance in [heater_power.md](heater_power.md).
- **Keep the ADC away from the buck converters, with separated grounds.** This reinforces (doesn't change) the star-ground and "locate converters away from the sensor array" guidance already in [heater_power.md](heater_power.md) §PCB layout guidelines — it extends that principle explicitly to the ADS131M08 pair, not just the sensor buffers.
- **General zoning:** power electronics grouped toward the **bottom** of the board, signal/sensing electronics (buffers, RC filters, ADCs, sensor array) toward the **top** — gives a consistent noisy/quiet split across the whole board rather than just around individual converters.

---

## 5. Sensor array physical layout

Two families of sensor-array arrangement have been explored; a comparison graphic (ring option "C2" vs. a straight grid) was shared alongside this feedback. Earlier exploration files are included for reference: [brian2_ring_layout_options.svg](pcb_layout/brian2_ring_layout_options.svg) (concentric-ring options A/B/C) and [brian2_ring_layout_options_v2.svg](pcb_layout/brian2_ring_layout_options_v2.svg) (revised concentric-ring options A/B/C, converging on "Two Rings by Rail" — outer = 2.5 V (5 sensors), inner = 1.8 V (9 sensors) — as the strongest ring candidate, labelled **C2** in the latest comparison).

### Option 1 — Concentric ring (C2: 1.8 V outer / 2.5 V inner)

- Two rings around the central BME680 reference: **1.8 V ring, 9 sensors, ≤ 321 mW total** (outer or inner per the latest graphic — outer in the labelled comparison), **2.5 V ring, 5 sensors, ≤ 250 mW total**.
- Every sensor on a ring is equidistant from the BME680 reference — equal radial exposure.
- The hottest sensors (50 mW each — GM-202B, GM-302B, GM-502B, GM-512B, GMV-2021B, all 2.5 V) sit nearest the BME680 in this arrangement.
- Single-rail-per-ring keeps routing and ground return clean (per the "Two Rings by Rail" rationale in the v2 exploration file).

### Option 2 — Straight grid (3×5)

- 5 columns × 3 rows; rows 2–3 (10 positions, 9 used) carry the 1.8 V rail, row 1 the 2.5 V rail — BME680 sits in the centre position of the middle row.
- Center-to-sensor distance ranges from 1.0× to 2.2× the grid pitch (less uniform radial exposure than the ring, but a simpler layout).
- Rows/columns map cleanly onto ADC channel banks (useful given the ADS131M08 pair is organised as two 8-channel devices).
- Can be **rotated 90°** if the board is narrower in one direction than the other — worth checking against the enclosure/board outline before committing to an orientation.

**Open:** which of these two goes into the layout hasn't been decided — both have real advantages (ring: equal thermal/radial exposure and simpler single-rail routing per ring; grid: simpler mechanical layout and clean ADC-channel mapping). Recommend deciding after checking both against the actual board outline and enclosure constraints.

### Separable sensor sub-board (open question — needs input)

Under discussion: splitting the sensor array onto a **separate sub-board** from the rest of BRIAN 2.0, joined by a row of board-to-board connectors along a **snappable perforated line** (see [perforated-connectors.pdf](pcb_layout/perforated-connectors.pdf)), so the sensor section can be swapped or re-aligned in a physical device without replacing the whole PCB.

- **Open question — connector pitch.** Needs input from David Cuartielles (MaU/Arduino) on whether the board-to-board connectors along the perforated line should be **2.54 mm** pitch, or something finer like **1.27 mm** or **1.0 mm**. Tradeoffs to weigh once he weighs in: pin count needed per connector (each sensor sheet carries roughly power + heater-drive + buffered analog-out, i.e. a handful of nets per sensor, ×14 sensors if the whole array splits off at once, or fewer if it splits per-group), connector cost/availability at each pitch, and mechanical strength of the snap line at a given connector density. **This is not yet decided — flagging for the team.**
- If adopted, this interacts with the ring-vs-grid decision above: a contiguous rectangular sub-board (grid layout) is a simpler shape to snap off cleanly than an arc of a ring, so the two decisions may need to be made together.

---

## 6. Consistency check against current markdown docs

The two component changes above are not yet reflected in the docs that specify them. Flagging here rather than silently rewriting, since these are prototype-stage decisions and the docs below make explicit part recommendations that a later reader would otherwise take at face value:

| Doc | Current text | Now superseded by |
| --- | --- | --- |
| [V2_design.md](V2_design.md) — Signal Readout Architecture, "Key parameters" table | `Buffer op-amp: RRIO CMOS dual, Ib < 10 pA, Vos < 1 mV (e.g. OPA2334, TLV2372)` and `unity buffer (RRIO CMOS, <10 pA Ib, SOT-23-8 dual)` | §1.1 above — TLV9152, SOT-23-THN package |
| [sensor_output_architecture.md](sensor_output_architecture.md) — Buffer Op-Amp Selection Criteria | `Package: SOT-23-8 dual preferred for density`; `Suitable parts: OPA2334 ..., TLV2372 ...` | §1.1 above |
| [pwm_driver.md](pwm_driver.md) — §4 Low-Side Driver Specifications | `Recommended Driver IC: Toshiba TBD62083APG (8-Channel DMOS FET Array)` | §1.2 above — TBD62083AFNG, SSOP package |
| [V2_design.md](V2_design.md) — Heater Power Architecture callout | References `TBD62083APG` by name | §1.2 above |
| [schematic_review_2026-07-03.md](schematic_review_2026-07-03.md) | Reviewed the 26G3 (2026-07-03) schematic export | Now superseded by the 26G7 (2026-07-07) export — see "Latest schematic revision" above; the review's findings haven't been re-checked against 26G7 |

Small pointer annotations have been added at the relevant spots in `V2_design.md` and `pwm_driver.md` linking back to this doc, following the existing "supersedes" callout style used elsewhere in those files. The full part/package rationale lives here rather than being duplicated inline.

No conflicts were found for the battery connector, buttons, or floorplan guidance — none of that is currently documented anywhere in `hardware/`, so this doc is the first record of those decisions.

---

## 7. Checklist

| # | Item | Type |
| --- | --- | --- |
| 1 | Swap buffer op-amp to TLV9152 (SOT-23-THN); verify Ib/Vos against datasheet; add SOT-23-THN footprint | component + footprint |
| 2 | Swap DMOS driver to TBD62083AFNG (SSOP); add SSOP18 footprint | component + footprint |
| 3 | Add JST-PH + screw-terminal + header footprint options at the battery connector location | footprint |
| 4 | Move ESP32 BOOT/EN/user buttons to right-angle edge-mount SMD, grouped on one side (RP2040 switch may stay put) | placement |
| 5 | Zone the board: power (charger/buck-boost/heater bucks) toward the edge and bottom; signal/sensing toward the top; rear side kept clear except non-solderable jumpers | floorplan |
| 6 | Align buck converters with their DMOS driver arrays; keep ADC(s) away from bucks with separated ground | floorplan |
| 7 | Decide ring (C2) vs. grid (3×5, possibly rotated) sensor layout against actual board outline/enclosure | decision needed |
| 8 | Get connector pitch input from David Cuartielles for the separable sensor sub-board (2.54 / 1.27 / 1.0 mm) | **blocked on input** |
| 9 | Re-run the [schematic_review_2026-07-03.md](schematic_review_2026-07-03.md) checklist against the 26G7 (2026-07-07) schematic export | re-verify |
