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
other top-level block is optional. The reference reader recognizes eleven
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
| `format_version` | **YES** | String | Quoted `"MAJOR.MINOR"`; baseline `"1.0"`. Readers are tolerant of a same-major higher minor (warn), reject a different major. See the validation rules below. |
| `assembly` | **YES** | Object | Assembly metadata |
| `technologies` | NO | Map | Technology definitions |
| `connection_stacks` | NO | Map | Named interconnect stacks (see [Connection Stacks](#connection-stacks)) |
| `components` | NO | Array | List of components |
| `interposer` | NO | Object | Interposer-axis adapter: which interposer PDK rule set the assembly is verified against (see [Interposer](#interposer)) |
| `interconnect` | NO | Object | Interconnect-axis adapter and optional technology (see [Interconnect](#interconnect)) |
| `interfaces` | NO | Array | Typed die-to-die / bond interfaces (see [Interfaces](#interfaces)) |
| `netlist` | NO | Object | Optional assembly netlist (see [Netlist](#netlist)) |
| `flow` | NO | Object | Optional, opaque build/flow block (see [Flow](#flow)) |
| `_metadata` | NO | Object | Intermediate-file marker (see [Intermediate files](#intermediate-files-_metadata)) |

Key order is not significant: readers are key-driven and accept the sections in
any order. (The reference writer emits them in the order `format_version`,
`_metadata`, `assembly`, `technologies`, `interconnect`, `connection_stacks`,
`components`, `interfaces`, `netlist`, `flow`; the KiCad exporter emits
`interposer` right after `assembly`, before `interconnect`.)

### Machine-readable schema

[`schemas/chiplet.schema.json`](../schemas/chiplet.schema.json) is the
machine-readable form of this document and is **normative for structure**: which
root keys exist (the table above, with `additionalProperties: false`, so an
undeclared root key is a schema error), which block each key holds, the key set
and types of every block this document defines by key list, and the closed
vocabularies (component `anchor`, component `orientation`, `interfaces[].type`,
the adapter-id pattern).

The **reference reader stays normative for semantics**: the tolerant
`format_version` policy, the `_metadata.finalize_required` refusal, the absent
`anchor` default-and-warn, the `1e5` um leak guard, and every cross-reference
check. A document can therefore be schema-valid and still be refused by a reader
(for example a different major, which is a policy question, not a structural
one), and in two documented cases the reader is deliberately the more tolerant of
the two: it carries an undeclared root key additively, and it coerces an unquoted
numeric `format_version` through `str()` for back-compat. Both cases are pinned
as fixtures in `conformance/`, where a *new* disagreement between the schema and
the reader fails the gate.

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
| `stackup` | NO | String | `""` | Path to a layer-stackup YAML this technology ships |
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
    stackup: "${INTERPOSER_PDK_ROOT}/libs.tech/chiplet_studio/intm4tm2.stackup.yaml"
    dbu: 0.001

  organic_substrate:
    description: "Organic substrate technology"
    dbu: 1.0
```

### Notes

- Technology IDs must be unique within the file
- `layer_properties` paths are resolved relative to the `.chiplet` file location
- `stackup` is resolved through the same chain as `layer_properties`: `${VAR}`
  expansion first, then an absolute path is taken as-is and a relative path is
  taken relative to the `.chiplet` file location. A writer round-trips the
  verbatim string it read, not the resolved one
- `stackup` lets a technology the consumer has no built-in stackup for still
  render with a real one. Where a consumer has its own lookup keyed on the
  technology id, an explicit `stackup` takes priority over it
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
| `attachment_surface_z` | NO | Float | - | **Interposer only.** Z of the die-attachment (BEOL-top) surface in the component-local frame: the plane dies mount on (`z_die = attachment_surface_z + connection height`). Decouples the mount reference from `dimensions.thickness` (the physical body). When absent, consumers fall back to `dimensions.thickness`. See the [coordinate-frame contract](./coord_frame_contract.md), sections 1.5 and 3. |
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

For an **interposer**, `thickness` is the physical substrate body
(Si + BEOL), and the die-mount plane is given separately by
[`attachment_surface_z`](#common-fields); the body extends *downward*
from that surface. Legacy interposer files omit `attachment_surface_z`
and overload `thickness` as the mount reference (its historical
meaning); readers fall back to `thickness` when the field is absent, so
those files still seat dies correctly. See `coord_frame_contract.md`
sections 1.5 and 3.4.

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

## Interposer

Optional top-level block declaring the **interposer axis** for the assembly:
which interposer PDK rule set the assembly is verified against. It carries a
single required key, `adapter`.

```yaml
interposer:
  adapter: intm4tm2                # interposer adapter id (required when present)
```

| Key | Required | Type | Description |
|-----|----------|------|-------------|
| `adapter` | **YES** | String | Interposer adapter id, e.g. `intm4tm2` |

`adapter` is a **registry id that the consuming ADK resolves against its own
adapter registry; it is never a filesystem path**. It must match
`^[A-Za-z0-9_][A-Za-z0-9_.-]*$` (so it carries no path separator and no leading
dot) and must not end in `.drc`. An id that is really a path, or a rule-deck
filename, ties a portable assembly document to one machine's directory layout;
a document that travels names *what* it needs, not *where* that thing sits on
the author's disk.

The block is written by the KiCad exporter (from the `INTERPOSER_ADAPTER`
project text variable, defaulting to `intm4tm2`), and read by the ADK assembly
DRC runner and by the assembly cockpit.

**A consumer that needs an adapter and does not find this key refuses**; it does
not fall back to a built-in default. A silently defaulted rule set means an
assembly is signed off against rules nobody chose, and the failure is invisible
in the output. Consumers that only *report* the axis (a viewer, a project
browser) may of course treat it as absent.

`interposer` and [`interconnect`](#interconnect) are two independent axes: the
first says which interposer rule set applies, the second which interconnect
(bump/pillar) rule set. Neither implies the other, and an assembly may declare
one, both, or neither.

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

`adapter` follows the same rule as [`interposer.adapter`](#interposer): a
registry id the consumer resolves, matching `^[A-Za-z0-9_][A-Za-z0-9_.-]*$` and
not ending in `.drc`, never a filesystem path.

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
that the format leaves unspecified, and readers preserve it verbatim across a
load/dump cycle.

Four rules, because this block is the only place the format carries something a
host might act on:

1. A reader MAY parse it. A reader that cannot parse it MUST NOT reject the
   document: the content is unspecified, so failing to understand it is the
   expected case, and the document is still valid without an executable flow.
2. The block is not authorization. It names nothing a reader is required to run.
   A host that executes what it names is responsible for establishing that the
   block is one it or its operator intended, because a `.chiplet` can arrive
   from anywhere and the document itself is the least trustworthy thing in the
   room. Authorship is not a usable test: a re-export copies a foreign block
   into a freshly generated file, so "our tool wrote this" survives an exchange
   that "our operator meant this" does not.
3. Nothing else in the format may be made to depend on it. It stays removable.
4. A host that re-emits a flow block it did not author MUST re-emit it byte for
   byte, unless the user edited the flow through that host. In particular a host
   MUST NOT write back resolved values of `${...}` variables, and MUST NOT drop
   keys it does not model: the block is opaque precisely so it can carry what a
   given host does not understand, and a lossy round-trip defeats that. The
   reference readers deliver the block as raw text for this reason; a host with
   a parsed flow model keeps the original text beside it and writes the text.
   "The block" is the run of lines the
   [top-level block grammar](#top-level-block-grammar-normative) below assigns
   to `flow`, which is where byte for byte gets its meaning.

```yaml
flow:
  steps:
    - {name: export, tool: kicad}
    - {name: assemble, tool: hyp_to_gds}
```

## Top-level block grammar (normative)

Flow rule 4, the merge tooling that carries a foreign block across a re-export,
and the block-ownership guard all need one answer from a document: which run of
lines belongs to which top-level key. That question is answered on the TEXT,
never on a parsed node tree, because a node tree re-quotes scalars and drops
comments on the way back out. It is answered here once, so that the merge
splitter in the KiCad plugin, the Python reference reader and the C++ reference
reader implement one grammar rather than three.

### Key line

A top-level block starts at a KEY LINE. A line is a key line if and only if its
content matches, anchored at both ends,

```
^([A-Za-z0-9_][A-Za-z0-9_.-]*):(?:\s.*)?
```

with capture group 1 as the key. Line CONTENT is the text up to the next LF with
one optional trailing CR removed, so a CRLF document reads exactly like an LF one;
the terminator is never part of what is matched.

Three notes, each of which has already cost this format family a defect:

- End the expression with `\Z` (Python `re`) or `(?![\s\S])` (portable, and the
  spelling `schemas/chiplet.schema.json` uses for the same reason), or match the
  whole content (`std::regex_match`, which needs no end anchor). NEVER `$`: in
  Python `$` also matches before a trailing newline, so a `$`-anchored
  implementation and an ECMA-262 one read different grammars from the same
  expression.
- ECMAScript `.` excludes CR as well as LF, unlike Python's, so a `std::regex`
  implementation writes `[^\n]` where the expression above writes `.`. On a line,
  which by definition carries no LF, the two mean the same thing.
- A line ends at LF, and nowhere else. Python's `str.splitlines()` also breaks on
  CR, VT, FF, U+0085, U+2028 and U+2029, so a splitter built on it grows a
  top-level block out of any quoted scalar carrying one of those: an
  `assembly.name` of `"demo<U+2028>flow: injected"` becomes a `flow` block that is
  not in the file, and a merge then hands a foreign host bytes that came out of
  somebody's assembly name. Iterate on LF by hand. The oracle carries this case.

What follows from the expression, each of these a case in the oracle: an indented
`  flow:` is not a key line and stays inside the block it sits in; `flow: value`
and `flow: # note` are key lines (a value or a trailing comment on the key line
changes nothing); `flow:value` is NOT, because the expression requires whitespace
between the colon and anything after it; and `---`, `...`, a `#` comment, an empty
line, a bare `:` and a key containing a space are not key lines.

### Top-level keys are written bare

`"flow":` and `'flow':` at column zero are valid YAML and are NOT key lines under
this grammar. A splitter therefore attaches such a block to the PRECEDING one,
where it is owned by whoever owns that block and is dropped by the next re-export
that regenerates it.

So a WRITER MUST emit top-level keys bare. This is the same rule as validation
rule 7, seen from the other side: rule 7 forbids fixing scalar quoting with a
document-wide emitter switch precisely because such a switch quotes keys too, and
this is what a quoted key costs. Keys stay bare, values are quoted.

A READER is bound differently. A quoted key at column zero makes a document NON
SPLITTABLE, not invalid: it is well-formed YAML, the schema has nothing to say
about how a key was spelled, and a reader that rejected it would be stricter than
this specification. A host that splits MUST refuse to SPLIT such a document
rather than mis-attribute the block, and MUST NOT write it back (see below).

The writer rule is therefore a countermeasure, not a prohibition: it keeps a
document from reaching the shape in which the ownership guard cannot answer, and
nothing in the schema forbids that shape.

### Block extent

A block's text runs from its key line up to but EXCLUDING the next key line, or to
end of file. Three consequences, all of them normative:

- Blank lines and full-line comments between two blocks are not key lines, so they
  attach to the PRECEDING block. A comment written directly above `flow:` belongs
  to the block before it, not to `flow`. This is deliberate: it is what the merge
  tooling already does, and a splitter that guessed otherwise would move bytes
  between two blocks with different owners.
- Lines before the first key line (a leading `---`, a file header comment) are a
  PREAMBLE that belongs to no key. An implementation that exposes the preamble
  uses the empty string as its key.
- A repeated top-level key is ILL-FORMED, and a reader MUST refuse the document at
  load: PyYAML resolves such a key to the last value and yaml-cpp to the first, so
  two conforming readers read different documents from one file, and neither the
  schema nor a parsed node tree can see that it happened. The refusal is on the
  text, because that is the only place the repeat still exists. A splitter could
  concatenate the two runs under the first occurrence without dropping a byte,
  which is what this specification used to say; that decides who owns the text and
  says nothing about which value wins, so it is no longer enough.

### Raw block text

A raw-block accessor returns the bytes exactly as they stand in the source: the
key line included, original line endings, no trailing-newline normalisation, no
re-indentation, nothing stripped. Both reference readers provide one, and that is
what makes flow rule 4 implementable:

- Python: `chiplet_format_io.top_level_blocks(text)` returns every block in
  document order, and `top_level_block(text, "flow")` returns one or `None`.
- C++: `ChipletDocument::flow_yaml` is the source slice of the `flow` block, key
  line included.

### A block the grammar cannot delimit

A `flow` block can be spelled so that YAML sees it and this grammar does not: a
flow-style document (`{format_version: "1.0", ..., flow: {...}}`), a key line
written `flow :`, or any file that is not splittable at all because it carries a
quoted key at column zero. The block then has no slice, and rule 4's byte for
byte is not a thing that can be done to it.

Three obligations follow, and they are deliberately not the same obligation:

- A host MUST still LOAD the document. Flow rule 1 already says a reader that
  cannot parse the block must not reject the file; not being able to *delimit* it
  is a weaker failure than not being able to parse it, so it cannot justify a
  stronger response. What is lost is a write guarantee, not the document.
- A host MUST NOT write the document back without re-authoring that block. The
  bytes were never captured, so writing is a choice between dropping the block
  and inventing it; a host that cannot make that choice honestly refuses to save
  and says why. Re-authoring the flow through the host is the way forward: then
  the host owns the block and rule 4 no longer applies to it.
- A host MUST NOT emit a re-serialisation of the parsed node in the place the
  source text belongs. A node dump re-quotes scalars and drops comments and is
  indistinguishable, in the file, from the source it replaced. This is the
  failure mode the raw-block accessors exist to prevent, and it is worse than
  refusing because nothing downstream can detect it.

The C++ reference records which of the three states a document is in
(`ChipletDocument::flow_source`: `Absent`, `Slice`, `NotDelimitable`), keeps
`flow_yaml` empty in the third, and throws from `dumps()`. A writer with no
source slice at all, such as the Python reference's canonical `dumps()`, never
promised rule 4 in the first place and is not bound by this.

### One oracle

Every implementation is measured against
[`conformance/fixtures/top_level_blocks_cases.json`](../conformance/fixtures/top_level_blocks_cases.json),
never against another implementation: accept and reject key lines, documents with
the exact expected slice per top-level key, the documents a splitter must refuse
to split, and the documents whose `flow` block has no slice. The three verdicts
are recorded separately, because a document can be loadable and not splittable,
and splittable and not writable. Add a case to that file, never to a consumer.

## Data Types and Constraints

### Numeric Values

| Type | Unit | Description |
|------|------|-------------|
| Coordinates (x, y, z) | micrometers | Floating-point position values |
| Dimensions (width, height, thickness) | micrometers | Floating-point, must be >= 0 |
| `attachment_surface_z` | micrometers | Floating-point (interposer only); typically > 0 |
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
consuming tool. Structural validation of the document shape itself is available
separately and mechanically, through
[`schemas/chiplet.schema.json`](../schemas/chiplet.schema.json) (see
[Machine-readable schema](#machine-readable-schema)); the rules below are the
ones that need a reader, not a schema.

**Enforced by the reference validator (hard errors):**

1. `format_version` is checked by a tolerant policy, not by exact-string equality.
   The on-disk baseline stays `"1.0"` (the format grows additively under it). A
   reader accepts the same major with a minor at or below the one it supports;
   accepts a same-major *higher* minor with a warning (reading it as the
   supported version and ignoring unknown additions); and rejects a different
   major (higher or lower) or a malformed value as a hard error. The field MUST
   be a quoted `"MAJOR.MINOR"` string; an unquoted numeric is coerced through
   `str()` for back-compat, which is exactly where PyYAML (`1.10` -> `"1.1"`) and
   yaml-cpp (`1.10` -> `"1.10"`) diverge, so quote it. A *lossless* passthrough
   writer preserves a same-major higher minor it was handed (the stamped version
   must describe the bytes written); a *lossy* writer that reconstructs from a
   struct model stamps the supported version, because it may have dropped
   fields it did not understand.
2. `assembly.name` is required and must not be empty.
3. Every component has a non-empty `id` and a non-empty `type`.
4. Every `interfaces[]` entry has an `id` and a `type`, and the `type` is one of `micro_bump`, `copper_pillar`, `tsv`, `wire_bond` (an unknown type is rejected). *C++ reference only; the Python reference validator does not check the interface-type vocabulary and accepts an unknown type.*
5. Every `netlist.nets[]` entry has a `name`. *C++ reference only; the Python reference validator does not check this and accepts a nameless net.*
6. A file whose top-level `_metadata.finalize_required` is `true` is refused unless intermediate files are explicitly allowed (it is not yet in the canonical frame; run the named `finalizer`).
7. Quoting is document semantics, not an emitter default. A writer MUST quote EVERY
   scalar the format defines as a string (`format_version`, component and pad `id`s,
   `assembly.name`, `created`, `modified` and other names and dates, `technologies`
   keys, `top_cell`, layout and file paths, adapter ids), unconditionally, without
   looking at the value. The rule is deliberately not "quote when the bare form would
   be re-typed": that condition would make the writer model the resolution tables of
   every YAML version and loader, make correctness depend on the value, and be
   untestable. The unconditional rule has a one-line self-test that discriminates
   exactly the property at stake: a writer parses its own output and every field the
   schema declares as a string comes back as a string. The class is wide: under a
   YAML 1.1 loader `0755` and `012` are integers, `0x1F` is an integer, `1.10` and
   `.inf` are floats, `no`, `on`, `off`, `yes` are booleans, `null` and `~` are null,
   and `2026-03-22` is a date, while yaml-cpp hands the same bytes back as strings,
   so two conforming readers name different components from one file. The guaranteed
   instance is not an odd id: `created` and `modified` are declared strings described
   as ISO 8601 dates, every real document quotes them, and a writer that emits them
   bare turns them into dates for every PyYAML consumer on the next save. A reader
   MUST NOT rely on coercion to recover the string; the reference reader's coercion
   of an unquoted `format_version` is back-compat for files that predate this rule,
   not a permitted form, and the conformance suite classifies that spelling as a
   schema negative. Emitters that quote only when YAML forces it (yaml-cpp's default,
   PyYAML's default for most scalars) do not satisfy this rule without an explicit
   quoting style for those fields. That style is applied per VALUE, never as a
   document-wide emitter switch: a switch that also quotes mapping keys (yaml-cpp's
   `Emitter::SetStringFormat(DoubleQuoted)` does) emits `"format_version":` at
   column zero, a spelling that is valid YAML but that the line-level ownership
   grammar of the merge tooling does not recognise as a key (see [Top-level block
   grammar](#top-level-block-grammar-normative): the document still loads, but it
   can no longer be split or written back), so a writer that fixed its scalars
   that way would defeat the block-ownership guard on the next merge.
   Keys stay bare; values are quoted. The examples in this specification and in
   `examples/` quote every such field, because writers are copied from examples.

**Reader behavior (warnings / fallbacks, not hard errors):**

7. New files SHOULD declare `anchor:` on every component; a reader defaults an absent `anchor` to `bbox_center` and warns.
8. If `connection` is specified it SHOULD name a known interconnect method; an absent or unresolved `connection` falls back to the interposer's `attachment_surface_z` (or `dimensions.thickness` when that field is absent, for legacy files) -- see the [coordinate-frame contract](./coord_frame_contract.md), section 3.
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
| 1.0 | 2026-08-05 | Documented the optional technology `stackup` field: a path to a layer-stackup YAML the technology ships itself, resolved through the same `${VAR}`/relative chain as `layer_properties` and taking priority over a consumer's own stackup lookup for that technology id. The field was already read, written and relied on by Chiplet Studio; it had never been written down here, so the reference C++ reader dropped it on a round-trip while a consumer's vendored copy carried it. Updated the C++ reference (struct/parse/emit); the Python reader already passes it through. Backward compatible and optional; `format_version` stays `"1.0"`. |
| 1.0 | 2026-07-21 | Added the optional interposer `attachment_surface_z` field: the die-attachment (BEOL-top) mount plane, decoupled from `dimensions.thickness`, which now carries the physical substrate body (extending downward from the attachment surface). Backward compatible: consumers fall back to `dimensions.thickness` as the mount reference when the field is absent, so legacy files seat dies unchanged. Updated the reference readers (C++ struct/parse/emit; the Python reader already passes it through) and the canonical example. No on-disk format change; `format_version` stays `"1.0"`. |
| 1.0 | 2026-09-01 | Added the optional top-level `interposer` block (a single required `adapter`, the interposer-axis registry id), taking the root key count from ten to eleven. The block was already emitted by the KiCad exporter and read by the ADK DRC runner and the cockpit; it had never been written down here, so it was an undocumented root key travelling between three tools. Fixed the `adapter` value as a registry id, never a filesystem path (pattern, no `.drc` suffix), and stated the consumer rule: refuse when an adapter is needed and absent, never default silently. Added [`schemas/chiplet.schema.json`](../schemas/chiplet.schema.json), normative for structure, with the reference reader still normative for semantics; wired it into the conformance gate over the whole committed corpus, with the schema-vs-reader divergences pinned. Backward compatible and optional; `format_version` stays `"1.0"`. |
