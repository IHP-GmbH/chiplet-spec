<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# `.chiplet` <-> 3Dblox / IEEE P3537 Interoperability (non-normative)

> **Status: informative appendix.** Nothing in this document changes the
> on-disk `.chiplet` format; `format_version` stays `"1.0"`. Where this
> document could appear to differ from
> [`CHIPLET_FORMAT_SPEC.md`](./CHIPLET_FORMAT_SPEC.md) or
> [`coord_frame_contract.md`](./coord_frame_contract.md), those documents
> govern.

## 1. What is being mapped

**3Dblox**, originated by TSMC and being standardized as **IEEE P3537**,
describes multi-die physical assemblies through three file kinds:

- **`.3dbv`** (vendor view): `ChipletDef` — per-die technology, design area
  and thickness, named bond regions, and linkage to P&R views (LEF, Verilog,
  Liberty).
- **`.3dbx`** (design view): the assembly — chiplet instances with 3D
  placement and orientation, region-to-region connections, and netlist
  linkage.
- **`.bmap`**: per-bump maps binding individual bump instances to ports/nets.

Its open reference implementation is **OpenROAD** (BSD-3): a multi-die
database, `read_3dbx` ingestion, an automatic assembly linter
(`check_3dblox`: floating components, 3D overlap, connection-region validity,
bump alignment, logical connectivity against a declared netlist, alignment
markers), and a 3D viewer.

The two formats describe the **same physical-assembly layer at different
abstraction levels**. 3Dblox binds the assembly to the P&R abstraction for
multi-die EDA and design-space exploration; it references no mask artwork.
`.chiplet` binds it to the mask level — GDS/OASIS bodies, `.lyp` layer
properties, per-layer interconnect metallurgy, fab DRC parameters — for
assembly signoff and fabrication hand-off. A design can usefully hold both: a
3Dblox view for exploration and P&R-level linting, a `.chiplet` for mask-level
assembly and DRC. This appendix defines the mapping between them.

## 2. Shared core

Both formats express: multi-die assemblies with a per-die technology; 2D
placement plus z; die thickness; flip / mounting orientation; die outlines;
black-box dies; assembly-level nets; explicit units and resolution; and
path/variable substitution in file references.

## 3. What 3Dblox expresses that `.chiplet` v1.0 does not

1. **Named bond regions with a side** (front / back / internal, plus an
   internal-extension variant), with coordinates and a layer.
2. **Per-bump maps** (`.bmap`) binding each bump instance to a port and net.
   (`.chiplet` carries pad *vocabulary* and interface *physics*, not a
   per-bump table.)
3. **Hierarchical sub-assemblies** (an assembly used as a component of a
   larger one, including unfolded views).
4. **Seal-ring / scribe** descriptions.
5. **Shrink** factors.
6. **Path assertions** (declared route-path checks across the assembly).
7. A **formal orientation group** (rotations by 90 degrees combined with
   mirrors). The genuinely missing delta in `.chiplet` is mirror-X /
   mirror-Y; the common cases are expressible today (`rotation.z` for
   rotations, `orientation: flip_chip` for the flipped mounting, see
   section 6).
8. **LEF / DEF / Verilog / Liberty / SDC linkage** — a non-goal for
   `.chiplet` by design; that is precisely the abstraction-level split.
9. A **linter-enforced invariant** that a connection's declared thickness
   equals the exact z gap between the connected surfaces. In `.chiplet` the
   same equality holds *by construction* of the z-mounting rule (section 6.1);
   there is no separate assertion to satisfy.

## 4. What `.chiplet` expresses that 3Dblox does not

1. **Per-layer interconnect metallurgy** (`connection_stacks`: name,
   material, height, diameter per layer) — and a die z **derived from the
   stack**, rather than a raw scalar gap.
2. **Interconnect method identity and provenance** (the `interconnect`
   adapter/technology axis and the `interconnect_methods.json` sidecar).
3. **Fab DRC parameter linkage** (fab parameters and pitch rules feeding a
   parameterized assembly-DRC flow).
4. **Mask artwork linkage**: GDS/OASIS bodies with `.lyp` layer properties.
   3Dblox references no artwork files.
5. **Polygonal boundary manifests** (arbitrary polygons with content hashes;
   3Dblox bond regions are 4-corner rectangles).
6. **Black-box pad vocabulary** for closed-PDK dies (pad layers usable
   without the die's own layer-properties file).
7. **Assembly-level wire-bond `io_pads`** and **typed physical interfaces**
   (`micro_bump` / `copper_pillar` / `tsv` / `wire_bond`, with pitch,
   diameter, height).
8. **`die_array`** components.
9. **Part-description linkage** (`cdxml_ref` to CDXML / JEDEC JEP30).
10. **Process-safety semantics**: the canonical coordinate frame with
    declared `anchor`, the coordinate leak guard, and the `_metadata`
    intermediate-file marker.
11. An opaque **`flow`** block for build/flow provenance.

## 5. Forward mapping (`.chiplet` -> 3Dblox), informative

A mechanical, **lossy-by-declaration** export is feasible. Field by field:

| `.chiplet` source | 3Dblox target | Notes |
|---|---|---|
| `die` component | `ChipletDef` (`.3dbv`) + instance (`.3dbx`) | one def per distinct die, one instance per placement |
| `dimensions.width` / `height` | design area | |
| `dimensions.thickness` | def thickness | **always** the physical thickness; never derived from any visualization stackup |
| `position` (geometric **center**) | instance placement | 3Dblox places by corner: convert center -> corner via the outline before emitting |
| `orientation: face_up` (default) | `R0` | |
| `orientation: flip_chip` | `MZ` (flipped mounting) | see section 6.3 |
| `orientation: face_down` | — | no mapping defined here; an exporter must pin and verify its own convention (section 6.3) |
| `rotation.z` | `R0` / `R90` / `R180` / `R270` | **only multiples of 90 degrees**; any other angle MUST fail the export loudly, never round silently |
| `connection` stack `total_height()` | connection thickness | exact value, by construction (section 6.1) |
| die / interposer bounding boxes | front-side 4-corner bond regions | polygonal boundary detail collapses to the bbox |
| `netlist` | structural Verilog (optional) | enables the logical-connectivity lint |
| `die_array` | unrolled individual instances | |
| `technologies` entries | per-die technology | requires an external technology LEF per technology (not derivable from `.chiplet`) |
| black-box pad coordinates | candidate `.bmap` rows | pad-vocabulary layers are per-pad-shaped data |

**Dropped by declaration** (present in `.chiplet`, no 3Dblox target):
per-layer metallurgy (collapses to the scalar thickness), method identity and
provenance, DRC parameter linkage, GDS/OASIS + `.lyp` artwork linkage,
polygonal boundary manifests beyond the bbox, `io_pads` / typed interface
physics, `cdxml_ref`, and the `flow` block.

**External inputs an export needs**: one technology LEF per die technology;
bump masters defined in the **die's** technology (not the substrate's) for
bump-level checks. Without per-die DEFs, dies export as black boxes and the
geometric consistency lints still run.

## 6. z and orientation conventions

These are the two places a naive converter produces a model that looks right
and fails the target linter (or worse, passes with wrong geometry).

### 6.1 Connection thickness is the stack height, exactly

The `.chiplet` z-mounting rule (contract, section 3) is
`z_die = mounting_surface + connection.total_height()`. The connection
stack's `total_height()` **is** the vertical gap between the attachment
surface and the die bottom. OpenROAD's linter asserts that a connection's
declared thickness equals that gap **exactly**. Therefore: emit
`thickness := connection.total_height()` — never a rounded or nominal value,
and never a gap re-derived from body outlines.

### 6.2 The attachment surface is not the maximum z

The `mounting_surface` resolves to the technology's **attachment surface**
(the pad top through the passivation opening), which is generally **not** the
substrate's maximum z: passivation can rise above the attachment surface
(in the reference interposer stackup, attachment at 13.83 um with the
passivation top at 15.73 um). A converter that derives the die-to-substrate
gap from outline max-z instead of the attachment surface disagrees with
`total_height()` by the passivation height and fails the exact-equality
check. Always use the attachment-surface / stack semantics of the contract.

### 6.3 Flip is a mounting intent, not a shared mirror convention

`.chiplet`'s `orientation: flip_chip` (die mounted face-down toward the
substrate) corresponds to 3Dblox's flipped orientation (`MZ`). But the 2D
*realization* differs between ecosystems: `.chiplet` writers realize the flip
as a layout mirror when emitting 2D views (spec, `orientation` section),
while 3Dblox composes a formal 3D orientation with a corner anchor. An
exporter must map the **intent** (`flip_chip` -> `MZ`) and then **verify that
pad/bump coordinates land at identical absolute positions** under the target
tool's orientation-plus-anchor composition; do not assume the mirror axes
agree. `face_down` is left unmapped here for the same reason: an exporter
that needs it must pin an explicit composition and verify pad positions.

### 6.4 Physical thickness has one source

A die's physical thickness is `dimensions.thickness`, always. Visualization
stackups (render slabs, display-only substrate heights) are aesthetic and
must never leak into an exported model.

## 7. Reverse direction (3Dblox -> `.chiplet`), informative

Coarse and of limited use: a `.chiplet` derived from 3Dblox has no GDS/OASIS
bodies, no metallurgy, and no DRC parameters, so it feeds neither a
mask-level viewer nor an assembly-DRC flow — it is a placement skeleton to be
enriched by hand. The forward direction is the productive one.

## 8. Source-of-truth rule

One assembly, one authoritative placement source. The `.chiplet` file is
authoritative for placement and z-mounting; a `.3dbx` produced by the mapping
in section 5 is a **derived export artifact**, regenerated rather than
edited. A project may record the export step in the opaque `flow` block. The
only proposed schema hook is the component-level
[`3dblox_ref`](./CHIPLET_FORMAT_SPEC.md#3dblox_ref-proposed-extension); an
assembly-level `.3dbx` reference is deliberately not proposed.
