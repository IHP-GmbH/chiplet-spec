<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH

This specification is published under the Apache License 2.0. Anyone may implement
readers, writers, or tools that consume or produce .chiplet files, under ANY license
(open-source or proprietary). IHP asserts no patents over the .chiplet format itself.
See README.md for the full implementer-rights / non-assertion grant.
-->

# Chiplet File Format Specification

**Version:** 1.0
**File Extension:** `.chiplet`
**Format:** YAML

## Overview

The `.chiplet` format is a YAML-based specification for describing 3D chiplet package assemblies. It defines the complete structure of a multi-die assembly including component placement, technology definitions, and layout references.

## Scope and relationship to other standards

`.chiplet` is a **physical-assembly layout** format. It answers *where each die,
interposer, and substrate sits in one shared coordinate frame, how each die is
z-mounted onto its interconnect, and which GDS/OASIS body and layer properties
each component carries*; the inputs an assembly viewer and an assembly DRC flow
need. It is the interchange pivot between PCB-style design entry and a 3D
assembly/DRC environment.

`.chiplet` is deliberately **not** a chiplet IP datasheet or a marketplace /
sourcing format. It does **not** model a chiplet's electrical, functional,
power, thermal, PHY/D2D-protocol, or test characterization. Those belong to a
part-description standard, a different layer of the stack where standards
already exist:

- **CDXML** (Chiplet Data Exchange in XML), a per-chiplet, machine-readable
  datasheet: pinout, mechanical envelope, electrical/ESD ratings, and D2D
  interface type. It was developed in **OCP / ODSA** and published under
  CC0-1.0, and its capabilities were folded into **JEDEC's JEP30** PartModel,
  a separate JEDEC standard that is now the active vehicle for this part-model
  data. CDXML describes *what a chiplet is*; by its own scope it carries no
  inter-die placement (its only coordinates are pad positions *within* a single
  part).

One other format family describes multi-die physical assemblies - the same
layer as `.chiplet`, bound to a different abstraction level:

- **3Dblox**, originated by TSMC and being standardized as **IEEE P3537**,
  with an open-source (BSD-3) implementation in OpenROAD (ingestion, an
  automatic assembly linter, a 3D viewer). 3Dblox binds an assembly to the
  **P&R abstraction** (LEF/DEF, Verilog netlists, Liberty views) for
  multi-die EDA and design-space exploration; it references no artwork files.
  `.chiplet` binds the same assembly layer to the **mask level** (GDS/OASIS
  bodies, `.lyp` layer properties, per-layer interconnect metallurgy with
  method identity, fab DRC parameters) for assembly signoff and fabrication
  hand-off. The overlap on the assembly core (multi-die placement, z,
  thickness, flip, per-die technology) is real and is deliberate interop
  territory: see [`3dblox_interop.md`](./3dblox_interop.md) for a
  field-by-field mapping and the
  [`3dblox_ref`](#3dblox_ref-proposed-extension) proposed extension below.

The three are **stages of one flow**, not competitors:

| | Part description (CDXML -> JEP30) | Assembly for P&R / exploration (3Dblox -> P3537) | `.chiplet` |
|---|---|---|---|
| Answers | *what is this part* (datasheet) | *how dies stack and connect*, over P&R views | *placed where, z-mounted how, mask-level DRC-ready* |
| Coordinates | per-pin pad x/y within one die | per-die 3D placement + bump maps over LEF geometry | per-component placement in a shared interposer frame; bodies as GDS/OASIS |
| Stage | part selection / sourcing | multi-die EDA / design-space exploration | physical assembly + fabrication signoff DRC |

A natural pipeline is: select a part described by CDXML / JEP30 -> place and
z-mount it in a `.chiplet` assembly -> run assembly DRC, with a 3Dblox view of
the same assembly derivable (via the mapping in the interop appendix) for
P&R-level exploration and linting. To make
these handoffs explicit, a `.chiplet` component may carry an optional
[`cdxml_ref`](#cdxml_ref-proposed-extension) citing the part it instantiates,
and an optional [`3dblox_ref`](#3dblox_ref-proposed-extension) citing the
3Dblox `ChipletDef` that describes the same die at the P&R level. The
canonical source of truth for each layer stays where it belongs: part data in
CDXML / JEP30, P&R views in LEF/DEF/3Dblox, mask-level placement in
`.chiplet` - a derived `.3dbx` assembly file is an export artifact, never a
second source of truth for placement (see the interop appendix).

## File Structure

A `.chiplet` file always carries `format_version` and an `assembly` block; every
other top-level block is optional. The reference reader recognizes ten
top-level keys in total (the full list, with required/optional status, is the
[Root Level Keys](#root-level-keys) table below). The most common skeleton is:

```yaml
format_version: "1.0"   # required

assembly:               # required
  # Assembly metadata

technologies:           # optional
  # Technology definitions

components:              # optional
  # Component list
```

---

## Root Level Keys

| Key | Required | Type | Description |
|-----|----------|------|-------------|
| `format_version` | **YES** | String | Format version, currently `"1.0"` |
| `assembly` | **YES** | Object | Assembly metadata |
| `technologies` | NO | Map | Technology definitions |
| `connection_stacks` | NO | Map | Named interconnect stacks (see [Connection Stacks](#connection-stacks)) |
| `components` | NO | Array | List of components |
| `interconnect` | NO | Object | Interconnect-axis adapter and optional technology (see [Interconnect](#interconnect)) |
| `interfaces` | NO | Array | Typed die-to-die / bond interfaces (see [Interfaces](#interfaces)) |
| `netlist` | NO | Object | Optional assembly netlist (see [Netlist](#netlist)) |
| `flow` | NO | Object | Optional, opaque build/flow block (see [Flow](#flow)) |
| `_metadata` | NO | Object | Intermediate-file marker (see [Intermediate files](#intermediate-files-_metadata)) |

Key order is not significant: readers are key-driven and accept the sections in
any order. (The reference writer emits them in the order `format_version`,
`_metadata`, `assembly`, `technologies`, `interconnect`, `connection_stacks`,
`components`, `interfaces`, `netlist`, `flow`.)

---

## Assembly Section

The `assembly` section contains metadata about the chiplet package.

### Fields

| Key | Required | Type | Default | Description |
|-----|----------|------|---------|-------------|
| `name` | **YES** | String | - | Display name of the assembly |
| `description` | NO | String | `""` | Long-form description |
| `author` | NO | String | `""` | Author/designer name |
| `created` | NO | String | `""` | Creation date (ISO 8601) |
| `modified` | NO | String | `""` | Last modification date (ISO 8601) |
| `units` | NO | String | `""` | Unit of measurement. The format sets no default; tools conventionally use `um`. |
| `assembly_gds` | NO | String | `""` | Path to a merged/flattened GDS of the whole assembly, when one is produced |
| `io_technology` | NO | String | `""` | Technology id used for assembly-level I/O pads |

### Example

```yaml
assembly:
  name: "HBM3 Package Assembly"
  description: "High-bandwidth memory stack with logic die"
  author: "Design Team"
  created: "2024-01-15"
  modified: "2024-02-20"
  units: "um"
```

---

## Technologies Section

The `technologies` section defines fabrication processes used by components. Each technology is identified by a unique ID (the map key).

### Fields

| Key | Required | Type | Default | Description |
|-----|----------|------|---------|-------------|
| `description` | NO | String | `""` | Technology description |
| `layer_properties` | NO | String | `""` | Path to KLayout `.lyp` file |
| `dbu` | NO | Float | `0.001` | Database unit in micrometers |

### Example

```yaml
technologies:
  tsmc_n5:
    description: "TSMC 5nm FinFET"
    layer_properties: "./tech/tsmc_n5.lyp"
    dbu: 0.001

  interposer_65nm:
    description: "65nm Silicon Interposer"
    layer_properties: "./tech/interposer.lyp"
    dbu: 0.001

  organic_substrate:
    description: "Organic substrate technology"
    dbu: 1.0
```

### Notes

- Technology IDs must be unique within the file
- `layer_properties` paths are resolved relative to the `.chiplet` file location
- `dbu` (database unit) defines coordinate resolution in the referenced layout files

---

## Components Section

The `components` section is an array of component definitions. Each component represents a physical element in the assembly.

### Common Fields

| Key | Required | Type | Default | Description |
|-----|----------|------|---------|-------------|
| `id` | **YES** | String | - | Unique component identifier |
| `type` | **YES** | String | - | Component type (see below) |
| `technology` | NO | String | `""` | Reference to a technology ID |
| `anchor` | NO* | String | `bbox_center` | How the component's mesh is centered: `gds_origin` or `bbox_center`. *Required for new files; readers warn and default to `bbox_center` when absent. See the [coordinate-frame contract](./coord_frame_contract.md), section 2. |
| `layout` | NO | String | `""` | Path to GDS/OASIS layout file |
| `top_cell` | NO | String | `""` | Top cell name in layout (single-cell form) |
| `cells` | NO | String or Array | - | One or more cell names in the layout. `top_cell` is the single-cell shorthand; writers emit `top_cell` for one cell and `cells` for several. |
| `position` | NO | Object | `{x:0, y:0, z:0}` | 3D position of the component's **geometric center**, in the canonical frame (see [Coordinate frame](#coordinate-frame-anchor-and-z-mounting-normative)). |
| `rotation` | NO | Object | `{z:0}` | Rotation angles |
| `orientation` | NO | String | `face_up` | Mounting orientation of a die: `face_up`, `flip_chip`, or `face_down`. Absent is treated as `face_up`. (`die` / `die_array`.) |
| `connection` | NO | String | `""` | Interconnect method id this die mounts on (e.g. `cupillar_opt1`); references an entry in the interconnect method registry (`interconnect_methods.json`). Drives per-die z-mounting (contract section 3). (`die` / `die_array`.) |
| `dimensions` | NO | Object | `{width:0, height:0, thickness:0}` | Physical size |
| `io_pads` | NO | Array | `[]` | Assembly-level I/O pads (e.g. wire-bond pads), nested under the `interposer`. See [io_pads](#io_pads-interposer-only). |
| `metadata` | NO | Map | `{}` | Custom key-value pairs |
| `array` | NO | Object | - | Array config (die_array only) |

### Component Types

| Type | Description |
|------|-------------|
| `die` | Single integrated circuit die |
| `die_array` | Array of identical dies (e.g., HBM stack) |
| `interposer` | Silicon interposer connecting multiple dies |
| `substrate` | Package substrate or carrier |

These four are the canonical/recommended values. The reference reader requires
only a non-empty `type` string and does not reject other values (see Validation
Rules).

### Position Object

```yaml
position:
  x: <float>    # X-coordinate in micrometers
  y: <float>    # Y-coordinate in micrometers
  z: <float>    # Z-coordinate (height) in micrometers
```

### Rotation Object

```yaml
rotation:
  z: <float>    # Rotation around Z-axis in degrees (0-360)
```

### Dimensions Object

```yaml
dimensions:
  width: <float>      # X-extent in micrometers
  height: <float>     # Y-extent in micrometers
  thickness: <float>  # Z-extent in micrometers
```

`thickness` is the physical Z-extent of the component body itself.
For dies it excludes the interconnect below the body: bump and pillar
heights are modeled by the referenced `connection_stacks` entry, and
the body extends upward from `position.z` (the seating plane; see
`coord_frame_contract.md` section 1.4).

### Array Configuration (for `die_array` type)

```yaml
array:
  pattern: <string>        # "grid", "linear", or "custom"
  count:
    x: <integer>           # Elements in X direction
    y: <integer>           # Elements in Y direction
  pitch:
    x: <float>             # X spacing in micrometers
    y: <float>             # Y spacing in micrometers
  start_position:
    x: <float>             # First element X position
    y: <float>             # First element Y position
    z: <float>             # First element Z position
```

### Coordinate frame, anchor, and z-mounting (normative)

The geometry fields above (`position`, `anchor`, `connection`, and the nested
`io_pads`) are governed by a single normative companion document:

> **[`coord_frame_contract.md`](./coord_frame_contract.md)** is the canonical
> source of truth for the `.chiplet` coordinate frame, the `anchor:` convention,
> and the per-die z-mounting rule. Every writer and reader MUST conform to it.
> This section only summarizes; where the two documents could appear to differ,
> the contract governs.

Summary of what the contract fixes:

- **Frame.** All `position:` x/y are expressed in the **GDS-bbox-corner of the
  interposer top_cell**, y-up cartesian, micrometers. `position:` is the
  component's **geometric center**, not its corner.
- **`anchor:`** declares how a component's local mesh is centered:
  `gds_origin` (mesh built around the component's own GDS (0,0); used by dies)
  or `bbox_center` (mesh centered on the component's GDS bbox; used by
  interposers). New files MUST declare it; readers default to `bbox_center`
  and warn when it is absent.
- **`connection:`** selects a die's interconnect method (an id in
  `interconnect_methods.json`). Z-mounting is per-die:
  `z_die = mounting_surface + connection.total_height()`, so one assembly can
  mix methods. A die with no `connection:` falls back to the interposer
  thickness (contract section 3).
- **Leak guard.** A reader MUST warn (or reject) when `|position.x|` or
  `|position.y|` exceeds `1e5` um, a heuristic that absolute (un-converted)
  coordinates have leaked through.

### orientation (die / die_array)

Optional string describing how a die is mounted. The reference reader treats it
as a canonical-string field with three recognized values: `face_up` (the
default), `flip_chip`, and `face_down`. An absent `orientation` is treated as
`face_up` downstream, and the reference C++ writer suppresses `face_up` on
output (it emits `orientation` only when the value is non-empty and not
`face_up`). The reader does not reject other strings, but writers should stay
within this vocabulary.

`orientation` records the assembly intent; the geometric effect of mirroring is
realized by the writer when it emits the layout (e.g. `gds_to_kicad
--flip-chip` mirrors X).

### io_pads (interposer only)

Assembly-level I/O pads (for example wire-bond pads) are listed under the
`interposer` component as `io_pads:`. Each pad is a point in the **same
canonical frame** as components (geometric center, um); pads do not declare
their own `anchor`. See the contract, section 6.

```yaml
io_pads:
  - id: J1                 # pad reference
    io_class: wire_bond    # pad class (free-form string)
    net: VDD               # net name (free-form)
    position: {x: 100.0, y: 100.0}
    size: {x: 80.0, y: 80.0}   # optional 2D extent
    layer: TopMetal2       # layer the pad sits on
```

Unlike `interfaces[].type`, `io_class` is a **free-form** string with no closed
vocabulary (`wire_bond` is a conventional value, not an enforced one). `size` is
optional; the reference C++ writer emits it for every pad on output.

### cdxml_ref (proposed extension)

> **Not part of the v1.0 schema.** `cdxml_ref` is a *proposed, optional*
> extension, documented here so the interop hook is well-defined; it is not one
> of the component fields the reference libraries model.

A `die` / `die_array` may cite the part it instantiates in a part-description
standard (CDXML / JEDEC JEP30) instead of duplicating that part's IP/datasheet
data here (see
[Scope and relationship to other standards](#scope-and-relationship-to-other-standards)).
The field is **optional and additive**: readers that do not understand it MUST
ignore it; its presence never changes placement, the coordinate frame, or
z-mounting.

```yaml
cdxml_ref:
  mpn: "ACME-PHY-0001"               # manufacturer part number (illustrative)
  opn: "ACME-PHY-0001-R"             # ordering part number (optional)
  version: "1.0"                     # part-document version (optional)
  uri: "./parts/acme_phy.cdxml"      # path or URL to the part document (optional)
  sha256: "<hex>"                    # content hash for provenance (optional)
```

At least one of `mpn` or `uri` SHOULD be present so the reference resolves. All
keys are strings.

Reference-library behavior today: the Python reference reader preserves the
field (it keeps the full mapping); the C++ struct reader does not round-trip it.
Until first-class support lands, treat `cdxml_ref` as opt-in metadata and do not
rely on the C++ reference reader to preserve it across a load/dump cycle.

### 3dblox_ref (proposed extension)

> **Not part of the v1.0 schema.** Like
> [`cdxml_ref`](#cdxml_ref-proposed-extension), `3dblox_ref` is a *proposed,
> optional* extension, documented here so the interop hook is well-defined; it
> is not one of the component fields the reference libraries model.

A `die` / `die_array` may cite the **3Dblox `ChipletDef`** that describes the
same physical die at the P&R abstraction level (see
[Scope and relationship to other standards](#scope-and-relationship-to-other-standards)),
so a mask-level assembly and a P&R-level view of the same die stay linked
without duplicating either. The field is **optional and additive**: readers
that do not understand it MUST ignore it; its presence never changes placement,
the coordinate frame, or z-mounting.

```yaml
3dblox_ref:
  chiplet: "cpu_die"              # ChipletDef name inside the .3dbv (recommended)
  uri: "./views/cpu_die.3dbv"     # path or URL to the .3dbv file (optional)
  sha256: "<hex>"                 # content hash for provenance (optional)
```

At least one of `chiplet` or `uri` SHOULD be present so the reference resolves.
All keys are strings.

The reference is deliberately **component-level only** (a die cites the
`ChipletDef` in a `.3dbv`). An *assembly*-level reference - to a `.3dbx`,
which re-states placements and connections for the whole assembly - is **not**
proposed: two machine-readable placement sources for one assembly would create
a source-of-truth ambiguity that the part- and die-level references do not
have. A project that keeps a derived `.3dbx` export alongside a `.chiplet` may
record the export step in the opaque [`flow`](#flow) block; the export is a
build artifact, and the `.chiplet` file remains authoritative for placement
and z-mounting. [`3dblox_interop.md`](./3dblox_interop.md) defines the mapping
such an export follows.

Reference-library behavior today: identical to `cdxml_ref` - the Python
reference reader preserves the field; the C++ struct reader does not
round-trip it. Treat `3dblox_ref` as opt-in metadata.

### Intermediate files (`_metadata`)

A writer that cannot yet emit the canonical frame (notably KiCad's GUI export,
which does not read the interposer GDS) emits an **intermediate** file marked
with a top-level `_metadata` block:

```yaml
_metadata:
  frame: pcb-bbox-corner
  finalize_required: true
  finalizer: hyp_to_gds.py --update-chiplet-file
```

A reader MUST refuse to load a file with `_metadata.finalize_required: true`
unless explicitly allowed; the finalizer converts positions into the canonical
frame and strips the block. Canonical `.chiplet` files carry no `_metadata`
block. See the contract, section 4.

### Component Example

```yaml
components:
  - id: substrate
    type: substrate
    technology: organic_substrate
    dimensions:
      width: 50000
      height: 50000
      thickness: 1000
    position:
      x: 0
      y: 0
      z: 0

  - id: interposer
    type: interposer
    technology: interposer_65nm
    layout: "./layouts/interposer.gds"
    top_cell: "INTERPOSER_TOP"
    dimensions:
      width: 40000
      height: 40000
      thickness: 100
    position:
      x: 5000
      y: 5000
      z: 1000

  - id: logic_die
    type: die
    technology: tsmc_n5
    layout: "./layouts/logic.gds"
    top_cell: "LOGIC_TOP"
    dimensions:
      width: 10000
      height: 10000
      thickness: 50
    position:
      x: 15000
      y: 15000
      z: 1100
    rotation:
      z: 0
    metadata:
      vendor: "TSMC"
      part_number: "ABC-123"

  - id: hbm_stack
    type: die_array
    technology: tsmc_n5
    layout: "./layouts/hbm_die.gds"
    top_cell: "HBM_DIE"
    array:
      pattern: grid
      count:
        x: 2
        y: 2
      pitch:
        x: 6000
        y: 6000
      start_position:
        x: 25000
        y: 15000
        z: 1100
```

---

## Connection Stacks

Optional top-level map of named interconnect stacks. Each entry describes the
physical layers a die's bump/pillar stack is built from; a die selects one by id
through its `connection:` field. In the reference toolchain the same stacks are
also published in the `interconnect_methods.json` sidecar (the single source of
truth for the interconnect PDK), and a `.chiplet` may inline the resolved stacks
here for self-containment.

```yaml
connection_stacks:
  cupillar_opt1:
    description: "PacTech Cu pillar, 35 um opening"
    layers:
      - {name: CuPillar, material: Cu,   height: 28.0, diameter: 44.0}
      - {name: SnAgCap,  material: SnAg, height: 16.0, diameter: 44.0}
```

| Key | Required | Type | Description |
|-----|----------|------|-------------|
| `description` | NO | String | Human-readable description |
| `layers` | NO | Array | Ordered stack layers, bottom (interposer side) first |
| `layers[].name` | - | String | Layer name (matched against the interposer stackup for z-mounting) |
| `layers[].material` | - | String | Material, e.g. `Cu`, `SnAg`, `SAC305` |
| `layers[].height` | - | Float (um) | Layer height; the stack total drives z-mounting (contract section 3) |
| `layers[].diameter` | - | Float (um) | Body diameter |

## Interconnect

Optional top-level block declaring the interconnect axis for the assembly. It is
recognized **only when it carries an `adapter`** (a block without `adapter` is
ignored). The optional `technology` gives the interconnect a PDK-backed identity
(same shape as a `technologies` entry); it is **registered under the adapter id**,
so it resolves through the same technology lookup as any other technology.

```yaml
interconnect:
  adapter: vendorx                 # interconnect adapter id (required when present)
  technology:                      # optional, PDK-backed identity (id = adapter)
    description: "VendorX microbump"
    layer_properties: ./tech/vendorx.lyp
    dbu: 0.001
```

## Interfaces

Optional top-level list of typed die-to-die / bond interfaces in the assembly.
Each interface has a required `id` and a required `type`; the `type` is validated
against a known vocabulary and an unknown value is rejected.

**Known `type` values:** `micro_bump`, `copper_pillar`, `tsv`, `wire_bond`.

```yaml
interfaces:
  - id: ucie_link_0
    type: micro_bump
    from: {component: U1, surface: top, port_layer: TopMetal2}
    to:   {component: interposer, surface: top, port_layer: Metal5}
    physical: {pitch: 45.0, diameter: 25.0, height: 24.0}
```

| Key | Required | Type | Description |
|-----|----------|------|-------------|
| `id` | **YES** | String | Unique interface id |
| `type` | **YES** | String | One of the known types above |
| `from` / `to` | NO | Object | Endpoints: `component`, `surface`, `port_layer` |
| `physical` | NO | Object | `pitch`, `diameter`, `height` (um) |

## Netlist

Optional top-level assembly netlist. Either inline `nets` or a path to an
`external_netlist`.

```yaml
netlist:
  external_netlist: ""             # optional path to an external netlist
  nets:
    - name: VDD
      class: power                 # net class; default "signal"
      external: true               # exposed at the assembly boundary
      connections:
        - {component: U1, pin: VDD, layer: TopMetal2}
        - {component: interposer, pin: P12, layer: Metal5}
```

| Key | Required | Type | Description |
|-----|----------|------|-------------|
| `nets[].name` | **YES** | String | Net name |
| `nets[].class` | NO | String | Net class; default `signal` |
| `nets[].external` | NO | Bool | `true` if the net is exposed at the assembly boundary |
| `nets[].connections` | NO | Array | `{component, pin, layer}` endpoints |
| `external_netlist` | NO | String | Path to an external netlist file |

An otherwise-empty `netlist` block (no `nets` and no `external_netlist`) is not
preserved on write.

## Flow

Optional, **opaque** build/flow block. It is host-specific build configuration
that the format leaves unspecified; readers preserve it verbatim across a
load/dump cycle and re-parse it only if they care.

```yaml
flow:
  steps:
    - {name: export, tool: kicad}
    - {name: assemble, tool: hyp_to_gds}
```

## Data Types and Constraints

### Numeric Values

| Type | Unit | Description |
|------|------|-------------|
| Coordinates (x, y, z) | micrometers | Floating-point position values |
| Dimensions (width, height, thickness) | micrometers | Floating-point, must be >= 0 |
| Rotation (z) | degrees | 0-360 |
| Array count (x, y) | - | Integer >= 1 |
| Array pitch (x, y) | micrometers | Floating-point spacing |
| DBU | micrometers | Typical: 0.001 (1nm resolution) |

### String Values

| Type | Format | Example |
|------|--------|---------|
| Identifiers | Alphanumeric + underscore | `logic_die_1` |
| Paths | Relative or absolute | `./layouts/die.gds` |
| Dates | ISO 8601 | `2024-01-15` |
| Cell names | Valid GDS cell name | `LOGIC_TOP` |

### Path Resolution

- **Relative paths** are resolved from the directory containing the `.chiplet` file
- **Absolute paths** are used as-is
- Supported layout formats: GDS, GDS2, OASIS

---

## YAML Formatting

Both compact (flow) and expanded (block) styles are supported:

```yaml
# Compact style
position: {x: 1000, y: 2000, z: 0}
dimensions: {width: 5000, height: 5000, thickness: 100}

# Expanded style
position:
  x: 1000
  y: 2000
  z: 0
```

---

## Validation Rules

The reference libraries (`chiplet-format-io`, Python and C++) enforce a small set
of structural invariants; file-system and cross-reference checks are left to the
consuming tool.

**Enforced by the reference validator (hard errors):**

1. `format_version` must be `"1.0"`.
2. `assembly.name` is required and must not be empty.
3. Every component has a non-empty `id` and a non-empty `type`.
4. Every `interfaces[]` entry has an `id` and a `type`, and the `type` is one of `micro_bump`, `copper_pillar`, `tsv`, `wire_bond` (an unknown type is rejected). *C++ reference only; the Python reference validator does not check the interface-type vocabulary and accepts an unknown type.*
5. Every `netlist.nets[]` entry has a `name`. *C++ reference only; the Python reference validator does not check this and accepts a nameless net.*
6. A file whose top-level `_metadata.finalize_required` is `true` is refused unless intermediate files are explicitly allowed (it is not yet in the canonical frame; run the named `finalizer`).

**Reader behavior (warnings / fallbacks, not hard errors):**

7. New files SHOULD declare `anchor:` on every component; a reader defaults an absent `anchor` to `bbox_center` and warns.
8. If `connection` is specified it SHOULD name a known interconnect method; an absent or unresolved `connection` falls back to the interposer thickness (see the [coordinate-frame contract](./coord_frame_contract.md), section 3).
9. A reader MUST warn (or reject) when any `position.x` or `position.y` exceeds `1e5` um in magnitude (heuristic for un-converted absolute coordinates).

**Consumer / tool-level (NOT enforced by the reference library, which never touches the filesystem):**

10. Component `id` values are expected to be unique.
11. Component `type` is expected to be one of the canonical values `die`, `die_array`, `interposer`, `substrate`; the reference reader accepts any non-empty string, so other values are allowed but not recommended.
12. If `technology` is specified it should reference a defined technology id.
13. If `layout` or `layer_properties` is specified, the referenced file should exist at the resolved path.

---

## Examples

### Minimal Valid File

```yaml
format_version: "1.0"

assembly:
  name: "Minimal Assembly"
```

### Complete Assembly

```yaml
format_version: "1.0"

assembly:
  name: "2.5D SoC Package"
  description: "HPC SoC with HBM3 memory"
  author: "Chiplet Team"
  created: "2024-06-01"
  units: "um"

technologies:
  logic_5nm:
    description: "5nm Logic Process"
    layer_properties: "./tech/logic_5nm.lyp"
    dbu: 0.001

  hbm_process:
    description: "HBM DRAM Process"
    layer_properties: "./tech/hbm.lyp"
    dbu: 0.001

  interposer:
    description: "65nm Interposer"
    layer_properties: "./tech/interposer.lyp"
    dbu: 0.001

components:
  - id: pkg_substrate
    type: substrate
    dimensions: {width: 55000, height: 55000, thickness: 1200}
    position: {x: 0, y: 0, z: 0}

  - id: si_interposer
    type: interposer
    technology: interposer
    layout: "./gds/interposer.gds"
    top_cell: "INTERPOSER"
    dimensions: {width: 45000, height: 45000, thickness: 100}
    position: {x: 5000, y: 5000, z: 1200}

  - id: soc_die
    type: die
    technology: logic_5nm
    layout: "./gds/soc.gds"
    top_cell: "SOC_TOP"
    dimensions: {width: 15000, height: 15000, thickness: 50}
    position: {x: 20000, y: 15000, z: 1300}
    metadata:
      power_tdp: "250W"
      io_count: "2048"

  - id: hbm_stack_0
    type: die_array
    technology: hbm_process
    layout: "./gds/hbm_die.gds"
    top_cell: "HBM_DIE"
    array:
      pattern: grid
      count: {x: 1, y: 4}
      pitch: {x: 0, y: 2500}
      start_position: {x: 8000, y: 20000, z: 1300}
    metadata:
      capacity: "16GB"
      bandwidth: "1TB/s"
```

---

## Software Template

Use this template to generate `.chiplet` files from your software:

```yaml
format_version: "1.0"

assembly:
  name: "<ASSEMBLY_NAME>"
  description: "<DESCRIPTION>"
  author: "<AUTHOR>"
  created: "<YYYY-MM-DD>"
  units: "um"

technologies:
  <TECH_ID>:
    description: "<TECH_DESCRIPTION>"
    layer_properties: "<PATH_TO_LYP>"
    dbu: <DBU_VALUE>

components:
  - id: "<COMPONENT_ID>"
    type: "<die|die_array|interposer|substrate>"
    technology: "<TECH_ID>"
    anchor: "<gds_origin|bbox_center>"
    orientation: "<face_up|flip_chip|face_down>"   # dies only; default face_up
    connection: "<INTERCONNECT_METHOD_ID|>"   # dies only; drives z-mounting
    layout: "<PATH_TO_GDS>"
    top_cell: "<CELL_NAME>"
    dimensions:
      width: <WIDTH_UM>
      height: <HEIGHT_UM>
      thickness: <THICKNESS_UM>
    position:                          # geometric center, canonical frame
      x: <X_UM>
      y: <Y_UM>
      z: <Z_UM>                        # 0 to defer to per-die auto z-mounting
    rotation:
      z: <ROTATION_DEG>
    metadata:
      <KEY>: "<VALUE>"
```

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2024-01 | Initial specification |
| 1.0 | 2026-06-19 | Documentation reconciled with the reference libraries and [`coord_frame_contract.md`](./coord_frame_contract.md). Documented previously-undocumented parts of the existing `format_version` 1.0 schema: `assembly.assembly_gds`/`io_technology`, component `anchor`/`orientation`/`connection`/`cells`/`io_pads`, geometric-center `position` semantics, the per-die z-mounting rule, the `_metadata` intermediate-file guard, and the top-level `connection_stacks`, `interconnect`, `interfaces`, `netlist`, and `flow` blocks. Reorganized Validation Rules into reference-enforced vs consumer-level. Added a "Scope and relationship to other standards" section (CDXML / OCP-ODSA / JEDEC JEP30) and a proposed, non-normative `cdxml_ref` extension. No on-disk format change; `format_version` stays `"1.0"`. |
| 1.0 | 2026-06-19 | Editorial pass: aligned the File Structure skeleton with the Root Level Keys table (one required `assembly` block plus optional blocks; ten recognized keys), documented the full `orientation` vocabulary (`face_up`/`flip_chip`/`face_down`, `face_up` default and writer suppression) to match the reference, and removed non-ASCII dashes. No normative change. |
| 1.0 | 2026-07-09 | Positioned `.chiplet` relative to 3Dblox / IEEE P3537 in the scope section (same physical-assembly layer; P&R vs mask abstraction; interop, not rivalry), added a proposed, non-normative `3dblox_ref` extension mirroring `cdxml_ref` (component-level only; no assembly-level `.3dbx` reference by design), and added the non-normative mapping appendix [`3dblox_interop.md`](./3dblox_interop.md). No on-disk format change; `format_version` stays `"1.0"`. |
