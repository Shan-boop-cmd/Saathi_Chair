# SAATHI chair: 3D-printable parts

`saathi_parts.scad` (OpenSCAD, free) holds four parametric parts; the STL files are ready to slice.
`parts_preview.png` shows them.

| Part | Qty | Job |
| --- | --- | --- |
| `leg_cup.stl` | 4 | The chair leg slides into the top socket (one M4 screw holds it). A 50 kg half-bridge load cell sits in the pocket underneath, held by its outer frame. |
| `leg_foot.stl` | 4 | Goes under each load cell. Its small centre boss presses only the cell's centre tab, so all the leg's load goes through the cell. Glue a 1 mm rubber pad in the recess underneath (non-slip). |
| `footrest_clamp.stl` | 2 | Bolts around each front leg (two M5 bolts). Its flat face takes a steel butt hinge; the footrest board (18 mm plywood with the four steel plates) screws to the other leaf and folds up. Because the footrest hangs from the legs, the weight of the feet passes through the load cells. |
| `finger_cradle.stl` | 1 | Screws onto the right armrest. The MAX30102 board sits in the pocket under a small window; the groove guides the finger and shades the sensor from room light. |

**Measure first.** Open `saathi_parts.scad` and set `leg_w`, `leg_d`, `leg_r` (your chair's leg tube), `cell_w`,
`cell_t`, `cell_centre` (your load cells) and `module_w`, `module_l` (your MAX30102 board). Then export each part:

```
openscad -D 'part="leg_cup"' -o leg_cup.stl saathi_parts.scad
```

**Printing:** PETG rather than PLA (PLA slowly creeps under a constant load, which drifts the weight reading).
4 walls, 40% infill, leg cups printed socket-up.

**Assembly checks:**

- With the chair empty, each leg must rest only on its load cell: nothing else should touch the floor.
- The load cells' wires (red, black, white per cell) join into one full bridge at the HX711: see the HX711 "4 half-bridge
  load cells" wiring in `hardware/README.md`.
- Run `tare` on the empty chair, then `cal 60` with a known 60 kg (two people of known weight, or sand bags) on the seat.
- Hinge, plywood and bolts come from a hardware shop and are in the parts list under "Footrest board, hinge, screws".
