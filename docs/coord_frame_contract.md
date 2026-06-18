<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# Coordinate Frame Contract — Chiplet Pipeline

**Owner:** chiplet-studio (reader and contract owner); all writers cite
this document by path.
**Companion:** [`CHIPLET_FORMAT_SPEC.md`](./CHIPLET_FORMAT_SPEC.md)
(general schema — this doc adds frame and anchor semantics).

---

## 0. TL;DR

1. All `position:` values in a `.chiplet` file are expressed in
   **GDS-bbox-corner of the interposer top_cell**, y-up cartesian,
   units micrometers.
2. `position:` is the component's **geometric center**, not its corner.
3. Each component declares an explicit `anchor:` field:
   - `anchor: gds_origin` — the component mesh is built around its own
     GDS (0,0). Used by dies produced by `gds_to_kicad`.
   - `anchor: bbox_center` — the component mesh is centered on its own
     GDS bounding box. Used by interposers.
4. Z-mounting for dies on connection stacks is fixed by the formula
   in §3.
5. `hyp_to_gds.py --update-chiplet-file` is **mandatory** in the
   canonical path. KiCad's `pcbnew` GUI export produces an
   intermediate `.chiplet` whose positions live in the wrong frame
   (PCB-bbox-corner) and is not directly consumable by chiplet-studio.
6. Interposer `dimensions:` are the **board outline** (prBoundary
   189/0, drawn from KiCad Edge.Cuts) when present in the GDS;
   `position:` stays the full-GDS-bbox center (§1.5).

Any tool that writes a `.chiplet` file MUST conform to this contract.
Any tool that reads one MUST validate that the contract is followed
(or fail loudly).

---

## 1. Canonical Coordinate Frame

### 1.1 Definition

The canonical frame for `.chiplet` `position:` values is:

| Property | Value |
|---|---|
| Reference object | Interposer's GDS top_cell |
| Origin (0, 0) | Lower-left corner of the interposer's GDS bbox |
| X axis | Increases rightward |
| Y axis | Increases upward (y-up cartesian) |
| Units | Micrometers (µm) |
| Float precision | At least 6 decimal places (1 pm theoretical) |

### 1.2 Why GDS-bbox-corner

- The GDS file is the physical fabrication artifact. The interposer
  GDS is the ground truth of the layout.
- KLayout, the 3D scene in chiplet-studio, and any downstream
  packaging tool all consume positions as offsets within the GDS
  bbox.
- The PCB Edge.Cuts bounding box (which KiCad uses natively) **does
  not always match** the GDS bbox. Historically the wire-bond demo
  carried a hidden shift of (-200 µm, -780 µm) between the two frames
  even though widths and heights agreed to the µm. The GDS frame is
  the only one with no such hidden shift relative to what gets
  fabricated.
- Since the converter draws the board outline (Edge.Cuts → prBoundary
  189/0) into the interposer GDS, the GDS bbox *contains* the outline.
  When all drawn geometry sits inside the outline — the normal case —
  the canonical origin coincides with the board outline's lower-left
  corner, and the historical shift above is zero by construction.

### 1.3 Diagram (top-down view)

```
                                  +--------------------------+
                                  |  GDS bbox of interposer  |
                                  |     (the reference)      |
                                  |                          |
                                  |    (die center)          |
                                  |        +                 |
                                  |       /                  |
                                  |      / position.y        |
                                  |     /                    |
                                  | (0,0)----- position.x    |
                                  +--------------------------+
                                  ^
                                  |
                          GDS-bbox-corner = canonical origin
```

`(0,0)` is the lower-left of the interposer's GDS bbox. Every
`position:` x and y in the .chiplet is measured from this corner,
y-up, in µm.

### 1.4 Position semantics

`position:` always refers to the component's **geometric center**.

For a die of width 1000 µm and height 2000 µm placed with its
lower-left corner at (250, 250) inside the interposer:
```yaml
position:
  x: 750.0      # 250 + 1000/2
  y: 1250.0     # 250 + 2000/2
  z: 61.83      # see §3 for Z mounting
dimensions:
  width: 1000.0
  height: 2000.0
  thickness: 50.0
```

### 1.5 Interposer dimensions vs. position

The two fields of the interposer component answer different
questions and have different sources:

| Field | Source | Meaning |
|---|---|---|
| `dimensions: width/height` | bbox of prBoundary 189/0 (the board outline, drawn from KiCad Edge.Cuts) when the layer is present; bbox of all drawn geometry otherwise (legacy GDS) | The fab extent of the interposer — what viewers render as the substrate body |
| `position: x/y` | half of the **full** GDS bbox (all layers, outline included) | Where the mesh bbox center sits in the canonical frame (`anchor: bbox_center`, §2) |

When the outline contains all drawn geometry, the full bbox equals
the outline bbox and both fields describe the same rectangle. When
copper leaks outside the outline (a design error — the converter
warns loudly at export), `dimensions` keeps the true board size
while `position` follows the mesh center, preserving die/pillar
registry in the render at the cost of a shifted substrate body.

The reader uses `position` and `dimensions` together to compute the
3D world placement (see §5).

---

## 2. Anchor Convention

### 2.1 The `anchor:` field

Each component declares how its local mesh is built:

| Value | Meaning | Use cases |
|---|---|---|
| `gds_origin` | The component mesh is built around its own GDS (0, 0). The `position:` value is added to the GDS-origin of the cell, no extra centering. | Dies produced by `gds_to_kicad` (footprint anchor at GDS (0,0)). Any die where the layout author put the cell origin where the placement reference should be. |
| `bbox_center` | The component mesh is centered on its own GDS bounding box. `position:` places the bbox center. | Interposers. Components whose GDS layout uses absolute pcbnew-derived coordinates and whose meaningful "placement reference" is the geometric center. |

The anchor is **declared by the writer** based on knowledge of how
the component's GDS was produced. The reader does **not** infer it
from `ComponentType`.

### 2.2 Default behavior

If `anchor:` is absent:
- The reader must default to `bbox_center` and emit a warning.
- This is for legacy `.chiplet` files only; there is no
  auto-migration.
- New files MUST declare `anchor:` explicitly.

### 2.3 Example

```yaml
components:
  - id: interposer
    type: interposer
    technology: interposer_tech
    anchor: bbox_center
    layout: ./interposer.gds
    top_cell: TOP
    position:
      x: 1750.0      # half of GDS bbox width (= outline width when
      y: 2800.0      #   all geometry is on-board, see §1.5)
      z: 0
    dimensions:
      width: 3500.0  # board outline (prBoundary 189/0), see §1.5
      height: 5600.0
      thickness: 13.83

  - id: U1
    type: die
    technology: sg13g2
    anchor: gds_origin
    connection: cupillar_opt2
    orientation: flip_chip
    layout: ./Metal_Test.gds
    top_cell: Metal_Test
    position:
      x: 1954.124    # die center in interposer-local frame
      y: 2330.481
      z: 61.83       # see §3
    dimensions:
      width: 770.0
      height: 2606.339
      thickness: 0
```

---

## 3. Z-Mounting Rule

### 3.1 Formula

For dies that mount on a connection stack (cu-pillar, solder bump,
etc.):

```
z_die = mounting_surface + connection.total_height()
```

where:
- `mounting_surface` = `z_bottom` of the interposer stackup layer
  whose name matches the connection stack's first layer.
- `connection.total_height()` = sum of `height` for every layer in
  the connection stack.

### 3.2 Worked example (wire-bond demo)

Interposer technology: `interposer_tech` (IHP SG13G2 BEOL).
Die connection: `cupillar_opt2` (PacTech, two layers: CuPillar 32 µm
+ SnAgCap 16 µm = total 48 µm).

`cupillar_opt2.layers[0].name` = `CuPillar`. The interposer stackup
contains a layer named `TopMetal2` whose `z_bottom = 13.83`. The
connection stack physically attaches to TopMetal2.

The connection stack's first layer name (`CuPillar`) does not appear
in the interposer stackup — the common case for visualization-only
stacks. The lookup then falls back as follows:

```
1. Look up cupillar_opt2.layers[0].name = "CuPillar" in interposer
   stackup. NOT FOUND.
2. Fallback: use the interposer's physical thickness as the mounting
   surface, OR — in the chiplet-studio implementation — the
   stackup's last "real" layer top (TopMetal2, z_top = 13.83).
3. mounting_surface = 13.83.
4. z_die = 13.83 + 48 = 61.83 µm.
```

### 3.3 Reference implementation

`chiplet-studio/src/core/Assembly.cpp::calculate_component_z`.

```cpp
// Mounting surface = z_bottom of the chosen connection stack's first
// layer (the layer that physically attaches to the interposer pad,
// e.g. CuPillar for cu-pillar stacks). Looking it up in the
// interposer stackup gives the exact passivation-opening / pad-top
// height. Adding stack->total_height() then lands the die on the
// tip of the connection.
```

### 3.4 Edge cases (must be handled by reader)

| Case | Behavior |
|---|---|
| Die has no `connection:` field | **Fallback**: use `interposer.thickness`. |
| `connection_stack` not defined in technology | **Fallback**: use `interposer.thickness`. |
| Connection's first layer not in interposer stackup | **Fallback**: use `interposer.thickness`. |
| Die has `position.z` explicitly set non-zero | Use the explicit value, do not auto-calculate. |

---

## 4. Writer Contract

Every tool that writes a `.chiplet` file MUST:
1. Express all `position:` x, y in the canonical frame (§1).
2. Use geometric center semantics (§1.4).
3. Declare `anchor:` explicitly per component (§2).
4. Set `z` to either the explicit user value, 0 to defer to
   auto-calc, or the auto-calculated value per §3.

### 4.1 KiCad `export_chiplet.cpp`

**Path:** `kicad/pcbnew/exporters/export_chiplet.cpp`

KiCad cannot produce the canonical frame on its own — it does not
read the interposer GDS. The pipeline therefore keeps two steps and
formalizes them: KiCad emits an **intermediate** file (positions in
PCB-bbox-corner), and `hyp_to_gds.py --update-chiplet-file` finalizes
it into the canonical frame.

KiCad's responsibilities:
- Add a `_metadata:` block at the top of the YAML so consumers know
  the file is intermediate:
  ```yaml
  _metadata:
    frame: pcb-bbox-corner
    finalize_required: true
    finalizer: hyp_to_gds.py --update-chiplet-file
  ```
- Emit `anchor:` per component (`bbox_center` for the interposer,
  `gds_origin` for dies that came from `gds_to_kicad`).
- chiplet-studio MUST refuse to load files where
  `_metadata.finalize_required: true`.

A tighter integration — KiCad invoking `hyp_to_gds.py` automatically
so a single action yields the canonical file — is deferred (see §8,
Future Work). It requires `hyp_to_gds.py` to be discoverable from
KiCad's runtime (PATH, plugin packaging, or bundling), which is a
distribution-level change.

### 4.2 `hyp_to_gds.py::update_chiplet_file`

**Path:**
`kicad_designs/kicad_interposer_hyperlynx_to_gds/hyp_to_gds.py`

This finalizer is the only place that owns the interposer GDS bbox,
so it performs the frame conversion:
- Interposer `position:` → GDS-bbox-center.
- Die `position:` → re-anchored to GDS-bbox-corner.
- io_pad `position:` → re-anchored to GDS-bbox-corner using the same
  `(gds_left, gds_bottom)` shift applied to dies:
  ```python
  for pad in component['io_pads']:
      pad['position']['x'] -= gds_left
      pad['position']['y'] -= gds_bottom
  ```
- Emit `anchor:` per component (interposer `bbox_center`, dies
  `gds_origin`; io_pads inherit the interposer frame and declare no
  anchor of their own).
- Cite this document in a comment near the conversion:
  `# Per chiplet-studio/docs/coord_frame_contract.md §1, position is
  in GDS-bbox-corner frame, geometric center.`
- Strip the `_metadata` block on output — the canonical `.chiplet`
  carries no `finalize_required` marker.

The interposer override and die re-anchor are the finalizer's
legitimate job: KiCad emits in PCB-bbox-corner and does not own the
GDS bbox, so converting PCB-bbox-corner → GDS-bbox-corner for any
design where PCB-bbox ≠ GDS-bbox must happen here.

### 4.3 `hyp_to_gds.py::add_io_pads` and the JSON producer

**Path:** `hyp_to_gds.py::add_io_pads` consumes `io_pads.json`
produced by `kicad_pcb_to_iopads.py`.

The JSON may remain in HYP-absolute coordinates — it feeds GDS-side
placement, which lives in absolute coords. The conversion to
GDS-bbox-corner is the responsibility of `update_chiplet_file`
(§4.2), which owns the GDS bbox lookup.

### 4.4 `gds_to_kicad` (footprint origin convention)

**Path:** `gds_to_kicad/gds_to_kicad.py`

The tool sets the KiCad footprint reference text at `(at 0 0)`,
placing the footprint origin at GDS cell (0, 0). Pads are placed at
their GDS-derived `(center_x, center_y)` relative to (0, 0). The
`--flip-chip` flag mirrors X only.

This convention is the reason dies declare `anchor: gds_origin` —
their GDS (0,0) is the reference point KiCad uses.

### 4.5 Future writers

Any new tool emitting `.chiplet` files (manual editor, plugin,
script) MUST cite this document by path in a comment near the writer
code. Reviewers MUST reject writers that don't.

---

## 5. Reader Contract

Every tool that reads a `.chiplet` file MUST:
1. Parse `anchor:` and store it on the component.
2. Use `anchor:` (not `ComponentType`) to drive mesh centering.
3. Validate position values are in plausible range; warn loudly if
   any `position.x` or `position.y` exceeds 1e5 µm (heuristic
   indicator of HYP-absolute leakage).

### 5.1 `chiplet-studio/src/formats/ChipletFormat.cpp`

- Parse the `anchor:` field per component (default to `bbox_center`
  on absence with `[chiplet] WARN: anchor not declared, defaulting to
  bbox_center`).
- Reject files declaring `_metadata.finalize_required: true` with a
  clear error: `error: this .chiplet is intermediate (PCB-bbox-corner
  frame); run hyp_to_gds.py --update-chiplet-file to finalize`.
- Validate `|position.x|, |position.y| < 1e5 µm` per component;
  warn on violation.

### 5.2 `chiplet-studio/src/view3d/LayerMeshBuilder.cpp`

Mesh centering is driven by the component's `anchor`, expressed as an
`Anchor` enum (`GdsOrigin` or `BboxCenter`) rather than a
caller-computed boolean. When the anchor is `BboxCenter`, the builder
centers the mesh on its GDS bounding box:

```cpp
if (has_polygons && anchor == Anchor::BboxCenter) {
    center_offset_x = (global_min_x + global_max_x) / 2.0;
    center_offset_y = (global_min_y + global_max_y) / 2.0;
}
```

The source of truth is the schema field, not a per-call flag.

### 5.3 `chiplet-studio/src/view3d/AssemblyView.cpp`

Resolve the anchor from the component, not from `ComponentType`:

```cpp
Anchor anchor = comp.anchor();
```

Pass `anchor` to `LayerMeshBuilder`. The chiplet → 3D world
coordinate mapping is:

```cpp
geometry.transform.translate(
    static_cast<float>(pos.x / 1000.0),    // chiplet X -> 3D X (mm)
    static_cast<float>(pos.z / 1000.0),    // chiplet Z -> 3D Y (mm)
    static_cast<float>(-pos.y / 1000.0));  // chiplet Y -> 3D -Z (mm)
```

The flipZ scale is orthogonal to anchor and unchanged.

### 5.4 `chiplet-studio/src/core/Component.h` / `IOPad.h`

- Add `enum class Anchor { GdsOrigin, BboxCenter }` in a new header
  `chiplet-studio/src/core/Anchor.h` (or inline in `Component.h`).
- Add `Anchor m_anchor = Anchor::BboxCenter` to `Component` with
  `anchor()` getter and `set_anchor()` setter.
- Add string conversion helpers:
  `anchor_to_string(Anchor)` and `string_to_anchor(const std::string&)`.
- Update the `IOPad` position doc comment from
  ```
  2D pad position in micrometers (interposer-global coordinates).
  ```
  to
  ```
  2D pad position in micrometers, in the canonical frame defined in
  chiplet-studio/docs/coord_frame_contract.md (interposer-local,
  GDS-bbox-corner).
  ```

### 5.5 `chiplet-studio/src/core/Assembly.cpp::calculate_component_z`

For dies with a missing `connection` or an undefined
`connection_stack`, fall back to `interposer.thickness` rather than
returning 0:

| Case | Behavior |
|---|---|
| `comp.connection().empty()` | `mounting_surface = interposer.thickness; total_height = 0` |
| `connection_stack(comp.connection())` returns null | same as above |
| First layer name not in interposer stackup | fall back to `interposer.thickness` |

The Z-mounting formula §3 holds in all cases.

---

## 6. io_pads Convention

### 6.1 Frame

io_pads `position:` lives in the same canonical frame as components
(GDS-bbox-corner of the interposer top_cell, geometric center of the
pad, µm). io_pads are nested under their interposer in the schema and
inherit the interposer's frame. They do **not** declare an `anchor:`
field — they are points (size is 2D extent, not a centering rule).

### 6.2 Validation

io_pads must be re-anchored to the canonical frame by the finalizer
(§4.2). A reader MUST warn (or reject) when `|position.x|` or
`|position.y|` exceeds 1e5 µm: the interposer GDS bbox is on the
order of a few thousand µm, so values that large indicate
HYP-absolute coordinates leaking through un-converted. io_pads are
stored as metadata and not currently rendered in 3D, but their
positions must still be canonical for any downstream consumer.

---

## 7. Verification Fixtures

Three fixtures, all required.

### 7.1 Synthetic fixture (unit test)

**Files:**
- `chiplet-studio/tests/fixtures/coord_contract_synth.chiplet`
- `chiplet-studio/tests/test_coord_frame_contract.cpp`

**Fixture contents:**
- 1 interposer, 1000 × 1000 µm, `anchor: bbox_center`,
  `position: (500, 500, 0)`.
- 2 dies of 100 × 100 µm thickness 50, `anchor: gds_origin`:
  - Die A at `position: (250, 250, 50)`.
  - Die B at `position: (750, 750, 50)`.
- 4 io_pads at the corners of the interposer:
  `(50, 50)`, `(950, 50)`, `(50, 950)`, `(950, 950)`.

**Test assertions:**
- `Component::anchor()` returns the parsed value for each component.
- After loading, the interposer's resolved 3D world position
  (computed via the same code path as `AssemblyView`) is
  `(0.5, 0.0, -0.5) mm` (or whatever follows from the canonical
  mapping; spell out the exact expected vector in the test).
- Die A's 3D world position is `(0.25, 0.05, -0.25) mm`.
- Die B's 3D world position is `(0.75, 0.05, -0.75) mm`.
- io_pad positions parse correctly and are stored verbatim.

### 7.2 Demo round-trip (integration test)

**Trigger:** regenerate `interposer_wire_bonding_demo.chiplet`
end-to-end via KiCad export + `hyp_to_gds.py --update-chiplet-file`.

**Test assertion (in `test_coord_frame_contract.cpp`):**
load the regenerated `.chiplet`, find U1, assert:
- `U1.anchor() == Anchor::GdsOrigin`
- `U1.position().z == 61.83 ± 0.01 µm` (sum of TopMetal2 z_top +
  cupillar_opt2 total_height)
- For each U1 pad-on-TopMetal2 in the U1 GDS, its world XY is
  within 1 µm of the closest cu-pillar SnAgCap cap center on the
  interposer.

This is the round-trip regression net for the contract.

### 7.3 KLayout-independent check (geometric verification)

**File:**
`kicad_designs/kicad_interposer_hyperlynx_to_gds/tests/check_complete_gds_alignment.py`

**Behavior:**
- Take a `*_complete.gds` path as argument.
- Use klayout pya to:
  - Find the `TOP` cell.
  - Locate U1's flipped instance (cell name pattern
    `*_flipped` or via the U1 reference in connection_stacks).
  - Compute U1's flipped-instance bbox in TOP coords.
  - Compute the cu-pillar array bbox in TOP coords (filter by the
    cu-pillar layer pair).
  - Assert: U1 bbox and cu-pillar array bbox overlap; centroid
    distance within 1 µm.
- Exit 0 on success, non-zero with a clear message on mismatch.

This script is independent of chiplet-studio so a chiplet-studio
bug cannot mask a real GDS misalignment.

### 7.4 Running all three

```bash
# 1. Unit tests (chiplet-studio synthetic + round-trip)
cd chiplet-studio/build
./tests/chiplet_tests --gtest_filter='CoordFrameContract*'

# 2. Demo regen
cd kicad_designs/kicad_interposer_hyperlynx_to_gds
python3 hyp_to_gds.py \
  --hyp ../../interposer_wire_bonding_demo/test.hyp \
  --update-chiplet-file ../../interposer_wire_bonding_demo/interposer_wire_bonding_demo.chiplet

# 3. KLayout-independent geometric check
python3 tests/check_complete_gds_alignment.py \
  ../../interposer_wire_bonding_demo/interposer_wire_bonding_demo_complete.gds
```

All three must pass before declaring this contract implemented.

---

## 8. Future Work

KiCad's "Export Chiplet" GUI action could integrate the
Hyperlynx + GDS pipeline so a single action produces the canonical
`.chiplet` without a separate `hyp_to_gds.py` invocation. All
information `hyp_to_gds.py` consumes is already present in the
Hyperlynx file KiCad produces. Scope:

1. KiCad's "Export Chiplet" action also emits the `.hyp` file.
2. The action invokes (or bundles) `hyp_to_gds.py
   --update-chiplet-file` to finalize.
3. The intermediate `_metadata.finalize_required: true` marker
   becomes unnecessary.

This is a refactor of the invocation flow, not a redesign of the
contract.

---

## Appendix A — Glossary of Coordinate Frames

The frames present in the toolchain:

| # | Frame | Origin | Used by |
|---|---|---|---|
| 1 | KiCad PCB internal nm | KiCad's signed int IU (nm) | KiCad core; not exposed to .chiplet |
| 2 | PCB-bbox-corner | Lower-left of `BoardEdgesBoundingBox` (or fallback) | KiCad `export_chiplet.cpp` for dies and io_pads |
| 3 | PCB-bbox-center | (PCB_w/2, PCB_h/2) | KiCad `export_chiplet.cpp` for the interposer |
| 4 | HYP absolute | Hyperlynx file native (meters, KiCad y-down convention pre-conversion) | `hyp_to_gds.py` input parsing |
| 5 | GDS absolute | GDS file native (µm, y-up cartesian) | KLayout, hyp_to_gds.py for cell instance placement |
| 6 | **GDS-bbox-corner** ← canonical | Lower-left of interposer GDS bbox | **THIS CONTRACT.** Also `update_chiplet_file` for dies. |
| 7 | GDS-bbox-center | (GDS_w/2, GDS_h/2) | `update_chiplet_file` for the interposer |
| 8 | Interposer-local-corner | The frame components mean to be in (per the `IOPad` position comment) | The schema's intent. Now formalized as = #6. |
| 9 | Chiplet-studio 3D world | OpenGL world (mm, y-up, z-out-of-screen) | `AssemblyView` final placement |

From the schema's perspective only #6 (canonical) and #9 (3D world)
are visible. The rest are internal to specific tools and never appear
in `.chiplet` files.

---

## References

- [`CHIPLET_FORMAT_SPEC.md`](./CHIPLET_FORMAT_SPEC.md) — general
  schema (companion document; this doc adds frame and anchor
  semantics).

---

## Version History

| Version | Date | Author | Changes |
|---|---|---|---|
| 1.0 | 2026-05-07 | Mauricio Montañares | Initial contract. Adopted GDS-bbox-corner as canonical frame; explicit `anchor:` field per component; Z-mounting formal definition; verification fixtures spec. |
