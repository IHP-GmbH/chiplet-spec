<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# Coordinate Frame Contract, Chiplet Pipeline

**Status:** canonical, part of the chiplet-spec format definition.
**Companion:** [`CHIPLET_FORMAT_SPEC.md`](./CHIPLET_FORMAT_SPEC.md)
(general schema; this doc adds frame and anchor semantics).
**Worked example:** [`examples/interposer_demo_design.chiplet`](../examples/interposer_demo_design.chiplet),
a self-contained instance of the frame, anchor, and z conventions
described here (mirrors sections 2.3 and 3.2).
**Reference libraries:** the repo-local, dependency-clean reference
reader/writer is `chiplet-format-io` (`reference/python/chiplet_format_io`,
`reference/cpp/`, Apache-2.0). These libraries are deliberately
**structural-only**: they parse, validate, and round-trip the schema (the
`format_version` gate, the intermediate-file guard, structural shape) and keep
`anchor:` as a raw string, but they do **not** implement the anchor defaulting,
the 1e5 range check, mesh centering, or z-mounting. Those frame semantics are a
consumer responsibility; this contract defines them, and the `chiplet-studio`
reference implementation (`src/formats/ChipletFormat.*` over a vendored copy of
the C++ library, plus `src/core/Assembly.cpp`, `src/view3d/LayerMeshBuilder.cpp`,
`src/view3d/AssemblyView.cpp`) demonstrates them. The writers are KiCad's
`export_chiplet.cpp`, `chiplet_kicad_plugin/hyp_to_gds.py`, and `gds_to_kicad`.
Paths of the form `chiplet-studio/...`, `kicad/...`, `chiplet_kicad_plugin/...`,
`gds_to_kicad/...` below name those downstream reference tools, illustrating how
the contract is met; they are not part of the format itself.

The problem this contract solves: a `.chiplet` file carries XY positions
that originate in several different coordinate frames (KiCad PCB, Hyperlynx,
GDS), and a die placed in the wrong frame lands tens or hundreds of microns
off its pads. Six alignment incidents traced back to exactly this. This
document pins one canonical frame, makes every writer convert into it, and
makes every reader fail loudly when something leaks through un-converted.

Any tool may implement this contract under any license; it is the
format-level companion to the `.chiplet` schema, not specific to one
implementation. In the reference toolchain the contract tests
(`test_coord_frame_contract.cpp`, `test_chiplet_format*.cpp` in chiplet-studio)
are the regression net that proves the behavior described here.

---

## 0. TL;DR

1. All `position:` values in a `.chiplet` file are expressed in
   **GDS-bbox-corner of the interposer top_cell**, y-up cartesian,
   units micrometers.
2. `position:` is the component's **geometric center**, not its corner.
3. Each component declares an explicit `anchor:` field:
   - `anchor: gds_origin`, the component mesh is built around its own
     GDS (0,0). Used by dies produced by `gds_to_kicad`.
   - `anchor: bbox_center`, the component mesh is centered on its own
     GDS bounding box. Used by interposers.
4. Z-mounting for dies on connection stacks is fixed by the formula
   in section 3. It is **per-die**: each die's `connection:` selects its
   own bump/pillar bodies, so one assembly can mix methods (section 3.2).
5. `hyp_to_gds.py --update-chiplet-file` is **mandatory** in the
   canonical path. KiCad's `pcbnew` GUI export produces an
   intermediate `.chiplet` whose positions live in the wrong frame
   (PCB-bbox-corner) and is not directly consumable by chiplet-studio.
6. Interposer `dimensions:` are the **board outline** (prBoundary
   235/0, drawn from KiCad Edge.Cuts) when present in the GDS;
   `position:` stays the full-GDS-bbox center (section 1.5).

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
| Units | Micrometers (um) |
| Float precision | At least 6 decimal places (1 pm theoretical) |

### 1.2 Why GDS-bbox-corner

- The GDS file is the physical fabrication artifact. The interposer
  GDS is the ground truth of the layout.
- KLayout, the 3D scene in chiplet-studio, and any downstream
  packaging tool all consume positions as offsets within the GDS
  bbox.
- The PCB Edge.Cuts bounding box (which KiCad uses natively) **does
  not always match** the GDS bbox. Historically the wire-bond demo
  carried a hidden shift of (-200 um, -780 um) between the two frames
  even though widths and heights agreed to the um. The GDS frame is
  the only one with no such hidden shift relative to what gets
  fabricated.
- Since the converter draws the board outline (Edge.Cuts to prBoundary
  235/0) into the interposer GDS, the GDS bbox *contains* the outline.
  When all drawn geometry sits inside the outline, the normal case,
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
y-up, in um.

### 1.4 Position semantics

`position:` refers to the component's **geometric center** in X and Y.

In Z, `position.z` is the component's **seating plane**: the bottom
face of the placed body, the one that meets the mounting surface (for
dies on a connection stack, the tip of that stack; see section 3).
`dimensions.thickness` extends the body upward from `position.z`.
Files that predate real thickness data carry the placeholder
`thickness: 0.0`, where the center and seating-plane readings
coincide; with a real thickness they differ by `thickness / 2`, and
the seating-plane reading is the normative one: it is what the
z-mounting rule computes and what consumers implement.

For a die of width 1000 um and height 2000 um placed with its
lower-left corner at (250, 250) inside the interposer:
```yaml
position:
  x: 750.0      # 250 + 1000/2
  y: 1250.0     # 250 + 2000/2
  z: 57.83      # seating plane; see section 3 for Z mounting
dimensions:
  width: 1000.0
  height: 2000.0
  thickness: 50.0   # body spans z 57.83 to 107.83
```

### 1.5 Interposer dimensions vs. position

The two fields of the interposer component answer different
questions and have different sources:

| Field | Source | Meaning |
|---|---|---|
| `dimensions: width/height` | bbox of prBoundary 235/0 (the board outline, drawn from KiCad Edge.Cuts) when the layer is present; bbox of all drawn geometry otherwise (legacy GDS) | The fab extent of the interposer, what viewers render as the substrate body |
| `dimensions: thickness` | KiCad stackup (`GetBoardThickness`) | The interposer's **physical body** z-extent (Si substrate + BEOL). It extends **downward** from `attachment_surface_z`: the die-mount plane is the top surface and the substrate body sits below it (local z goes negative). Legacy files without `attachment_surface_z` overload this field as the mount reference. |
| `attachment_surface_z` | interposer stackup `attachment_surface_z` (13.83 for IntM4TM2 = TopMetal2 top 10.83 + 3.00) | The die-attachment (BEOL-top) surface z in the interposer-local frame: the plane dies mount on, `z_die = attachment_surface_z + connection height` (section 3). Optional; when absent, consumers fall back to `dimensions.thickness` as the mount reference. |
| `position: x/y` | half of the **full** GDS bbox (all layers, outline included) | Where the mesh bbox center sits in the canonical frame (`anchor: bbox_center`, section 2) |

When the outline contains all drawn geometry, the full bbox equals
the outline bbox and both fields describe the same rectangle. When
copper leaks outside the outline (a design error; the converter
warns loudly at export), `dimensions` keeps the true board size
while `position` follows the mesh center, preserving die/pillar
registry in the render at the cost of a shifted substrate body.

The reader uses `position` and `dimensions` together to compute the
3D world placement (see section 5).

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

### 2.3 Example (from the wire-bond demo)

The canonical demo interposer uses `intm4tm2` (the IHP IntM4TM2
interposer stackup; `configs/stackups/intm4tm2.yaml`). Numbers below are
the live values from
`kicad_designs/interposer_wire_bonding_demo/interposer_wire_bonding_demo.chiplet`.

```yaml
components:
  - id: interposer
    type: interposer
    technology: intm4tm2
    anchor: bbox_center
    layout: interposer_wire_bonding_demo_interposer.gds
    top_cell: INTERPOSER
    position:
      x: 3246.156      # half of full GDS bbox width (= outline width
      y: 2801.000      #   when all geometry is on-board, see section 1.5)
      z: 0.0
    dimensions:
      width: 6492.312  # board outline (prBoundary 235/0), see section 1.5
      height: 5602.001
      thickness: 300.0    # physical interposer body (KiCad stackup); see section 1.5
    attachment_surface_z: 13.83   # BEOL-top die-mount plane; see sections 1.5, 3

  - id: U1
    type: die
    technology: sg13g2
    anchor: gds_origin
    connection: cupillar_opt1   # per-die method; see section 3.2
    orientation: flip_chip
    layout: ${GDS_TO_KICAD_ROOT}/.../Metal_Test.gds
    top_cell: Metal_Test
    position:
      x: 1954.121     # die center in interposer-local frame
      y: 2332.483
      z: 57.83        # 13.83 + 44 (cupillar_opt1), see section 3
    dimensions:
      width: 730.0
      height: 2566.339
      thickness: 750.0   # physical die body (Si bulk), see section 1.4
```

### 2.4 Orientation vocabulary

A component's `orientation:` field takes exactly two canonical values:

| Value | Meaning |
|---|---|
| `face_up` (default) | The die artwork is used as drawn; no mirror. Applied before `rotation.z`. |
| `flip_chip` | The die is mounted face-down onto the interposer; realized as an **x-mirror** of the die artwork (`mx = -1`), applied before `rotation.z`. |

`face_up` is the default when the field is absent. **`face_down` is not a
canonical token**: flip-chip mounting is expressed as `flip_chip` (the mirror
is the observable contract; "face down" is the physical picture, not the field
value). Writers MUST emit only `face_up`/`flip_chip`.

Reader obligations on a non-canonical or unknown `orientation:` token:

- **Validators and exporters** (e.g. the ADK `pads_vs_pillars` check and the
  `chiplet2dbx` interop exporter) MUST reject it — an ambiguous token must
  never silently yield wrong geometry.
- **Interactive viewers** MAY be lenient (keep rendering) but MUST warn; they
  MAY treat `face_down` as `flip_chip`. A viewer must never silently render an
  unknown token as `face_up` (un-mirrored).

---

## 3. Z-Mounting Rule

### 3.1 Formula

For dies that mount on a connection stack (cu-pillar, solder bump,
microbump):

```
z_die = mounting_surface + connection.total_height()
```

where:
- `mounting_surface` = `z_bottom` of the interposer stackup layer
  whose name matches the connection stack's first layer, after the
  interconnect-PDK fragment for this die's `connection:` is merged in
  (see section 3.2).
- `connection.total_height()` = sum of `height` for every layer in
  the connection stack.

### 3.2 Worked example, two-die mixed-method demo

The canonical demo is a **two-die, mixed-method** assembly: U1 seats on an
IHP cu-pillar stack and U2 on a non-IHP vendor microbump, both finalized in
one export. The method is **per-die**: each die's `connection:` id selects
its own interconnect-PDK fragment, so the dies seat at different heights from
the same `calculate_component_z`.

Interposer technology: `intm4tm2`. Its stackup declares
`attachment_surface_z: 13.83` (the TopMetal2 top through the passivation
opening; `configs/stackups/intm4tm2.yaml`).

How the mounting surface resolves (`Assembly::calculate_component_z`,
`src/core/Assembly.cpp`):

```
1. Take this die's connection id, e.g. cupillar_opt1.
2. Load the interposer stackup (intm4tm2) and MERGE the interconnect
   fragment for that connection id
   (interconnect_pdk/libs.tech/chiplet_studio/stackup_fragments/
    cupillar_opt1.stackup.yaml). The fragment declares
    z_reference: attachment_surface, so its CuPillar layer (local z 0.00)
    is offset by the stackup's attachment_surface_z (13.83). After merge,
    CuPillar.z_bottom = 13.83.
3. firstLayerName = connection.layers[0].name = "CuPillar". Look it up
   in the merged stackup. FOUND. mounting_surface = 13.83.
4. z_die = 13.83 + total_height(cupillar_opt1).
```

Per-die totals in the demo:

| Die | connection | stack layers | total_height | z_die |
|---|---|---|---|---|
| U1 | `cupillar_opt1` (IHP cu-pillar) | CuPillar 28 + SnAgCap 16 | 44 | 57.83 |
| U2 | `vendorx_microbump` (non-IHP) | VendorXBumpCu 18 + VendorXBumpCap 6 | 24 | 37.83 |

U1 selects Option 1 (not Option 2) because its pad ring has a 79.93 um-pitch
pair, legal at Option 1's 75 um minimum but below Option 2's 80 um. That
selection lives in the die's footprint `CONNECTION` field and rides through
the export; the contract tests `U1Position` / `U2Position` are the regression
witness.

### 3.3 Reference implementation

`chiplet-studio/src/core/Assembly.cpp::calculate_component_z`.

The fragment merge happens via
`stackup.mergeInterconnectFragments(LayerStackup::resolveInterconnectKeys({comp->connection()}, ...))`
before the first-layer lookup, so a die using any method seats on that
method's body heights even when sibling dies use other methods.

```cpp
// Mounting surface = z_bottom of the chosen connection stack's first
// layer (the layer that physically attaches to the interposer pad,
// e.g. CuPillar for cu-pillar stacks). After merging this die's
// interconnect fragment the layer resolves to attachment_surface_z;
// adding stack->total_height() then lands the die on the tip of the
// connection.
```

### 3.4 Edge cases (must be handled by reader)

| Case | Behavior |
|---|---|
| Die has no `connection:` field | **Fallback**: mount on `interposer.attachment_surface_z`, `connection_height = 0`. |
| `connection` id not a defined connection stack | **Fallback**: mount on `interposer.attachment_surface_z`, `connection_height = 0`. |
| Connection's first layer not in interposer stackup (even after merge) | **Fallback**: mount on `interposer.attachment_surface_z`. |
| `interposer.attachment_surface_z` absent (legacy file) | **Fallback**: use `interposer.dimensions.thickness` as the mount reference (the pre-split behavior, when thickness doubled as the attachment surface). |
| Die has `position.z` explicitly set non-zero | Use the explicit value, do not auto-calculate. |

The mount reference is `attachment_surface_z` when the interposer declares it,
falling back to `dimensions.thickness` for files written before the field
existed -- so both eras seat dies on the BEOL top, not on world z=0 (the
earlier `0.0` return was a bug that put dies at the origin) and not on the
substrate bottom (which is what reading the physical `thickness` would now
give). The fallback keeps the formula valid in all cases.

---

## 4. Writer Contract

Every tool that writes a `.chiplet` file MUST:
1. Express all `position:` x, y in the canonical frame (section 1).
2. Use geometric center semantics (section 1.4).
3. Declare `anchor:` explicitly per component (section 2).
4. Set `z` to either the explicit user value, 0 to defer to
   auto-calc, or the auto-calculated value per section 3.

### 4.1 KiCad `export_chiplet.cpp`

**Path:** `kicad/pcbnew/exporters/export_chiplet.cpp`

KiCad cannot produce the canonical frame on its own; it does not
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

A tighter integration, KiCad invoking `hyp_to_gds.py` automatically
so a single action yields the canonical file, is deferred (see section 8,
Future Work). It requires `hyp_to_gds.py` to be discoverable from
KiCad's runtime (PATH, plugin packaging, or bundling), which is a
distribution-level change.

### 4.2 `hyp_to_gds.py::update_chiplet_file`

**Path:** `chiplet_kicad_plugin/hyp_to_gds.py`
(`def update_chiplet_file` near line 1711; consumed by adk-tools as
`tools/chiplet_kicad_plugin/hyp_to_gds.py`).

This finalizer is the only place that owns the interposer GDS bbox,
so it performs the frame conversion:
- Interposer `position:` to GDS-bbox-center.
- Die `position:` re-anchored to GDS-bbox-corner.
- io_pad `position:` re-anchored to GDS-bbox-corner using the same
  `(gds_left, gds_bottom)` shift applied to dies. The finalizer rebuilds
  each component's `io_pads` list fresh from the JSON keys, subtracting the
  shift from the source `x_um`/`y_um`:
  ```python
  component['io_pads'] = [
      {
          'id': p.get('ref') or f"J{i+1}",
          'io_class': p.get('io_class', 'wire_bond'),
          'net': p.get('net', ''),
          'position': {
              'x': float(p.get('x_um', 0.0)) - gds_left,
              'y': float(p.get('y_um', 0.0)) - gds_bottom,
          },
          ...
      }
      for i, p in enumerate(io_pads)
  ]
  ```
  If no bbox is available it leaves them un-shifted (HYP-absolute) and warns.
- Emit `anchor:` per component (interposer `bbox_center`, dies
  `gds_origin`; io_pads inherit the interposer frame and declare no
  anchor of their own).
- Strip the `_metadata` block on output; the canonical `.chiplet`
  carries no `finalize_required` marker.

The interposer override and die re-anchor are the finalizer's
legitimate job: KiCad emits in PCB-bbox-corner and does not own the
GDS bbox, so converting PCB-bbox-corner to GDS-bbox-corner for any
design where PCB-bbox does not equal GDS-bbox must happen here.

### 4.3 `hyp_to_gds.py::add_io_pads` and the JSON producer

**Path:** `chiplet_kicad_plugin/hyp_to_gds.py::add_io_pads`
(`def add_io_pads` near line 1466) consumes `io_pads.json` produced by
`kicad_pcb_to_iopads.py`.

The JSON may remain in HYP-absolute coordinates; it feeds GDS-side
placement, which lives in absolute coords. The conversion to
GDS-bbox-corner is the responsibility of `update_chiplet_file`
(section 4.2), which owns the GDS bbox lookup.

### 4.4 `gds_to_kicad` (footprint origin convention)

**Path:** `gds_to_kicad/gds_to_kicad.py`

The tool sets the KiCad footprint reference text at `(at 0 0)`,
placing the footprint origin at GDS cell (0, 0). Pads are placed at
their GDS-derived `(center_x, center_y)` relative to (0, 0). The
`--flip-chip` flag mirrors X only (`mx = -1 if flip_chip else 1`),
generating the footprint as seen from the interposer side.

This convention is the reason dies declare `anchor: gds_origin`;
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
   any `position.x` or `position.y` exceeds 1e5 um (heuristic
   indicator of HYP-absolute leakage).

The repo-local reference libraries (`reference/cpp`, `reference/python`) do
**not** satisfy these reader requirements on their own: they are structural-only
(see the header), so they parse and round-trip `anchor:` as a raw string but
perform no anchor defaulting, no 1e5 range check, and no z-mounting. A reader
that needs the full contract must add the frame semantics on top, as the
`chiplet-studio` reference implementation does in the sub-sections below.

### 5.1 `chiplet-studio/src/formats/ChipletFormat.cpp`

`ChipletFormat::load` delegates parsing and structural validation to the
vendored reference library (`cfio::load`, `src/formats/chiplet_format_io/`)
and re-throws `cfio::ChipletFormatError` as `ChipletFormatException` so
callers' contracts hold. The library owns the YAML grammar, the
`format_version` gate, and the intermediate-file guard; `ChipletFormat.cpp`
adds the frame/anchor semantics:

- Parse the `anchor:` field per component. Absent leaves the `BboxCenter`
  default (`Component.h`, `m_anchor = Anchor::BboxCenter`) marked undeclared;
  present-but-unknown warns per component and is treated as undeclared.
  After loading, a single **file-level** summary lists every component that
  was defaulted: `N component(s) without explicit 'anchor' field; defaulted
  to bbox_center: <idlist>` followed by a pointer to this document, section 2.
- Intermediate-file rejection lives in the vendored library, not in
  `ChipletFormat.cpp`: `cfio::validate` throws when
  `_metadata.finalize_required: true` and `allow_intermediate` is false, with
  the message `this is an intermediate .chiplet (_metadata.finalize_required:
  true); run <finalizer> to finalize, or pass allow_intermediate=true`.
  `ChipletFormat::load` surfaces that as a `ChipletFormatException`; the net
  effect (refuse to load) is unchanged.
- Validate `|position.x|, |position.y| < 1e5 um` per component and per io_pad
  (`kPositionWarnThreshold_um = 1.0e5`); warn loudly on violation, citing
  section 1 (components) and section 6 (io_pads).

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

Pass `anchor` to `LayerMeshBuilder`. The chiplet to 3D world
coordinate mapping is:

```cpp
geometry.transform.translate(
    static_cast<float>(pos.x / 1000.0),    // chiplet X -> 3D X (mm)
    static_cast<float>(pos.z / 1000.0),    // chiplet Z -> 3D Y (mm)
    static_cast<float>(-pos.y / 1000.0));  // chiplet Y -> 3D -Z (mm)
```

The flipZ scale is orthogonal to anchor and unchanged.

### 5.4 `chiplet-studio/src/core/Component.h` / `IOPad.h`

The `Anchor` enum is defined inline in `Component.h`
(`enum class Anchor { GdsOrigin, BboxCenter };`); there is no separate
`Anchor.h`. `Component` holds `Anchor m_anchor = Anchor::BboxCenter` with an
`anchor()` getter, `set_anchor()` setter, and an `anchor_declared()` flag, plus
the `anchor_to_string` / `string_to_anchor` helpers. The `IOPad` position doc
comment (`IOPad.h`) documents the canonical frame:

```
2D pad position in micrometers, in the canonical frame defined in
chiplet-studio/docs/coord_frame_contract.md (interposer-local,
GDS-bbox-corner of the parent interposer's top_cell, geometric
center of the pad).
```

### 5.5 `chiplet-studio/src/core/Assembly.cpp::calculate_component_z`

For dies with a missing `connection`, an undefined connection stack, or a
first layer that does not resolve in the (merged) interposer stackup, fall
back to `interposer.thickness` rather than returning 0:

| Case | Behavior |
|---|---|
| `comp.connection().empty()` | `mounting_surface = interposer.thickness; connection_height = 0` |
| `connection_stack(comp.connection())` returns null | same as above |
| First layer name not in merged interposer stackup | `mounting_surface = interposer.thickness` |

The Z-mounting formula in section 3 holds in all cases.

---

## 6. io_pads Convention

### 6.1 Frame

io_pads `position:` lives in the same canonical frame as components
(GDS-bbox-corner of the interposer top_cell, geometric center of the
pad, um). io_pads are nested under their interposer in the schema and
inherit the interposer's frame. They do **not** declare an `anchor:`
field; they are points (size is 2D extent, not a centering rule).

### 6.2 Validation

io_pads must be re-anchored to the canonical frame by the finalizer
(section 4.2). A reader MUST warn (or reject) when `|position.x|` or
`|position.y|` exceeds 1e5 um: the interposer GDS bbox is on the
order of a few thousand um, so values that large indicate
HYP-absolute coordinates leaking through un-converted. io_pads are
stored as metadata and not currently rendered in 3D, but their
positions must still be canonical for any downstream consumer.

---

## 7. Verification Fixtures

Two fixture groups (synthetic + demo) live in the C++ contract test;
a third KLayout-independent geometric check lives next to the finalizer.

### 7.1 Synthetic fixture (unit test)

**Files:**
- `chiplet-studio/tests/fixtures/coord_contract_synth.chiplet`
- `chiplet-studio/tests/test_coord_frame_contract.cpp`
  (`CoordFrameContractSynth` fixture)

**Fixture contents:**
- 1 interposer, 1000 x 1000 um, thickness 13.83, `anchor: bbox_center`,
  `position: (500, 500, 0)`.
- 2 dies of 100 x 100 um thickness 50, `anchor: gds_origin`:
  - `U_A` at `position: (250, 250, 50)`.
  - `U_B` at `position: (750, 750, 50)`.
- 4 io_pads at the corners of the interposer:
  `(50, 50)`, `(950, 50)`, `(50, 950)`, `(950, 950)`, layer `TopMetal2`.

**Test assertions:**
- `Component::anchor()` returns the parsed value and `anchor_declared()`
  is true for each component.
- The 3D world mapping (the same `(x/1000, z/1000, -y/1000)` transform as
  `AssemblyView`) yields, with no tolerance:
  - interposer `(0.5, 0.0, -0.5) mm`
  - `U_A` `(0.25, 0.05, -0.25) mm`
  - `U_B` `(0.75, 0.05, -0.75) mm`
- io_pad positions, sizes, layer, and io_class parse correctly.

### 7.2 Demo round-trip (integration test)

**Trigger:** regenerate `interposer_wire_bonding_demo.chiplet`
end-to-end via KiCad export + `hyp_to_gds.py --update-chiplet-file`.

**Fixture:** `CoordFrameContractWirebondDemo` in
`test_coord_frame_contract.cpp`. The demo lives in the sibling
`kicad_designs/` tree, resolved from `$WIREBOND_DEMO_CHIPLET` or the default
workspace layout; the fixture skips with a clear message if neither is
reachable.

**Test assertions:** load the regenerated `.chiplet` and check:
- interposer `anchor() == Anchor::BboxCenter`,
  `U1.anchor() == Anchor::GdsOrigin`.
- `U1.position()` near `(1954.12, 2332.48, 57.83)` um
  (z = 13.83 attachment surface + 44 `cupillar_opt1`).
- `U2.position()` near `(5170.23, 2420.27, 37.83)` um
  (z = 13.83 + 24 `vendorx_microbump`); this locks the mixed-method case.
- interposer position near `(3246.16, 2801.00, 0.0)` um and dimensions near
  `6492.31 x 5602.00 x 13.83` (board outline per section 1.5).
- every io_pad sits inside the interposer extent (`0 <= x <= width`,
  `0 <= y <= height`): the regression net for the HYP-absolute leak.
- the file loads at all: had it still carried
  `_metadata.finalize_required: true`, `load` would have thrown.

This is the round-trip regression net for the contract.

### 7.3 KLayout-independent check (geometric verification)

**File:** `chiplet_kicad_plugin/tests/check_complete_gds_alignment.py`
(adk-tools mirror: `tools/chiplet_kicad_plugin/tests/`).

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
    distance within 1 um.
- Exit 0 on success, non-zero with a clear message on mismatch.

This script is independent of chiplet-studio so a chiplet-studio
bug cannot mask a real GDS misalignment.

### 7.4 Running all three

```bash
# 1. Unit + round-trip tests (chiplet_tests target lives at build/tests/).
#    Run from the chiplet-studio build dir.
./tests/chiplet_tests --gtest_filter='CoordFrameContract*'

# 2. Demo regen (paths relative to the sibling kicad_designs tree)
python3 chiplet_kicad_plugin/hyp_to_gds.py \
  --hyp kicad_designs/interposer_wire_bonding_demo/test.hyp \
  --update-chiplet-file \
    kicad_designs/interposer_wire_bonding_demo/interposer_wire_bonding_demo.chiplet

# 3. KLayout-independent geometric check
python3 chiplet_kicad_plugin/tests/check_complete_gds_alignment.py \
  kicad_designs/interposer_wire_bonding_demo/interposer_wire_bonding_demo_complete.gds
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

## Appendix A, Glossary of Coordinate Frames

The frames present in the toolchain:

| # | Frame | Origin | Used by |
|---|---|---|---|
| 1 | KiCad PCB internal nm | KiCad's signed int IU (nm) | KiCad core; not exposed to .chiplet |
| 2 | PCB-bbox-corner | Lower-left of `BoardEdgesBoundingBox` (or fallback) | KiCad `export_chiplet.cpp` for dies and io_pads |
| 3 | PCB-bbox-center | (PCB_w/2, PCB_h/2) | KiCad `export_chiplet.cpp` for the interposer |
| 4 | HYP absolute | Hyperlynx file native (meters, KiCad y-down convention pre-conversion) | `hyp_to_gds.py` input parsing |
| 5 | GDS absolute | GDS file native (um, y-up cartesian) | KLayout, hyp_to_gds.py for cell instance placement |
| 6 | **GDS-bbox-corner** (canonical) | Lower-left of interposer GDS bbox | **THIS CONTRACT.** Also `update_chiplet_file` for dies. |
| 7 | GDS-bbox-center | (GDS_w/2, GDS_h/2) | `update_chiplet_file` for the interposer |
| 8 | Interposer-local-corner | The frame components mean to be in (per the `IOPad` position comment) | The schema's intent. Now formalized as = #6. |
| 9 | Chiplet-studio 3D world | OpenGL world (mm, y-up, z-out-of-screen) | `AssemblyView` final placement |

From the schema's perspective only #6 (canonical) and #9 (3D world)
are visible. The rest are internal to specific tools and never appear
in `.chiplet` files.

---

## References

- [`CHIPLET_FORMAT_SPEC.md`](./CHIPLET_FORMAT_SPEC.md), general
  schema (companion document; this doc adds frame and anchor
  semantics).
- [`examples/interposer_demo_design.chiplet`](../examples/interposer_demo_design.chiplet),
  the canonical worked example: a runnable two-die mixed-method
  assembly whose positions, anchors, and z values mirror sections 2.3
  and 3.2 of this contract.
- Reference reader/writer library: `reference/python/` and
  `reference/cpp/` (chiplet-format-io, Apache-2.0). chiplet-studio
  consumes a vendored copy of the C++ library. These libraries are
  structural-only; see the header and section 5 for the consumer/library
  split on frame semantics.

---

## Version History

| Version | Date | Author | Changes |
|---|---|---|---|
| 1.0 | 2026-05-07 | Mauricio Montanares | Initial contract. Adopted GDS-bbox-corner as canonical frame; explicit `anchor:` field per component; Z-mounting formal definition; verification fixtures spec. |
| 1.1 | 2026-06-18 | Mauricio Montanares | Updated to the two-die mixed-method demo (U1 cupillar_opt1 z=57.83, U2 vendorx_microbump z=37.83); documented per-die fragment merge in calculate_component_z; corrected interposer technology to intm4tm2 and demo dimensions; moved finalizer/check paths to chiplet_kicad_plugin/; noted intermediate-file guard now lives in the vendored chiplet_format_io library; removed em-dashes. |
| 1.2 | 2026-06-18 | Mauricio Montanares | Relocated to chiplet-spec as the canonical, permissive (Apache-2.0) home, re-synced from the chiplet-studio copy; chiplet-studio now points here. Reframed as a format-level contract whose `chiplet-studio/...`, `kicad/...`, `gds_to_kicad/...` paths denote reference implementations. |
| 1.3 | 2026-06-19 | Mauricio Montanares | Named the repo-local `chiplet-format-io` libraries (`reference/cpp`, `reference/python`) as the primary reference and made explicit that they are structural-only, with the frame/anchor/z semantics owned by consumers and demonstrated by chiplet-studio (header and section 5). Cross-referenced the in-repo worked example `examples/interposer_demo_design.chiplet`. |
