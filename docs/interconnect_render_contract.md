<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# Interconnect rendering and die seating: data contract

How Chiplet Studio renders the 3D bodies of an interconnect method
(cu-pillars, solder bumps, vendor microbumps) and seats dies on them, and
the data contract this implies between the interposer PDK, the
interconnect PDK and this tool. Companion to `coord_frame_contract.md`
(coordinate frames) and the interconnect PDK's manifest (methods, layers,
DRC rules).

## Architecture

Three parties contribute, each through its own artifact:

| Party | Artifact | Declares |
|---|---|---|
| interposer PDK | technology stackup YAML | the substrate layers and the **attachment surface z** (`attachment_surface_z`) |
| interconnect PDK | `libs.tech/chiplet_studio/stackup_fragments/<method>.stackup.yaml` | the method's 3D **body layers** (GDS layer/datatype, heights), z relative to the attachment surface (`z_reference: attachment_surface`) |
| `.chiplet` | per-die `connection:` + `interconnect:` block | each die's **method** (= fragment key); the block carries the legacy/fallback **adapter** + the PDK-backed technology identity |

At load time the studio registers the interconnect method as a Technology
(keyed by the adapter id) so it appears alongside the die/interposer PDKs
with its own layer properties and provenance.

At render and z-calculation time the method's stackup fragment is merged
into the **interposer technology's** stackup — and only that one. The
merge is additive and idempotent (`addLayer` overwrites by layer key) and
is implemented once — `LayerStackup::mergeInterconnectFragment` — which
both consumers call, so their views of the body layers cannot diverge:

- `AssemblyView::buildLayerGeometry` — gives the body layers z/height in
  the interposer component's mesh.
- `Assembly::calculate_component_z` — locates the mounting surface (the
  z where a die's connection stack starts) by looking up the stack's
  first layer name in the merged stackup.

A die's seating height itself comes from the `.chiplet`'s inline
`connection_stacks` (per die), not from the fragment; the fragment is the
render-side and surface-lookup data.

## Conventions (load-bearing, keep them true)

**C1 — Body shapes live in the interposer's layout.** Generators place
the interconnect body polygons (e.g. 500/35, 501/35, or a vendor's
510/35, 511/35) inside the GDS that the interposer component references.
The studio renders them as layers of the interposer mesh whose elevations
come from the merged fragment. Any future generation flow must keep this
invariant or the bodies will not render.

**C2 — Fragment is keyed by method.** One fragment per interconnect
method (`cupillar_opt2.stackup.yaml`, …); a die's `connection:` id
selects it, so each option carries its own body heights. Adapter-keyed
fragments remain in the PDK as deprecated family-default fallbacks.
`LayerStackup::resolveInterconnectKeys` is the single policy point:
method ids that resolve win; the adapter is used only when none does,
and never overwrites resolved per-method values.

**C3 — Merge is scoped to the interposer technology.** Merging into any
other technology corrupts flip-chip rendering: a FaceDown die's stackup
`totalHeight()` is its z-inversion reference, and inflating it shifts
every die layer upward by the body-stack height.

## Known couplings and their state

**L1 — Fragment z is relative to the attachment surface (resolved).**
Fragments declare `z_reference: attachment_surface` and use z values
relative to 0, so they carry method-owned data only (body heights); the
interposer stackup YAML declares `attachment_surface_z` (for IntM4TM2:
13.83, the exposed pad top — TopMetal2 top at the passivation opening).
The shared merge helper applies the offset, so the same fragment seats
dies correctly on any interposer that declares its surface. Degraded
paths are loud, never silent: a fragment without the marker keeps the
legacy absolute interpretation (deprecation warning), and a base stackup
without the declaration gets a best-effort `totalHeight()` offset
(warning names the missing key). Note that the attachment surface is a
declared value, not `max_z` of the stackup — passivation geometry rises
above the real mounting surface.

**L2 — Method selection is per die (resolved).** One interconnect PDK
per assembly; what varies per die is the method within it. The per-die
`connection:` id selects the fragment: die seating merges only the die's
own method fragment (exact per die), the render path merges the union of
the methods present (body layer keys are disjoint across vendors by
manifest construction), and the assembly DRC scopes each method's
pitch/spacing rules to its dies' pads (see the ADK adapter contract,
"Per-method refinement"). Options of one family share GDS body layers,
so a mixed-option union renders the taller body per shared layer,
loudly — the per-layer render model can show one height per key; a truly
per-die body render belongs to L3's mesh-group work. The board is the
data source for the selection: each die footprint's `CONNECTION` field
(exposed per die in the export dialog) names its method, and the GDS
generator draws each die's body polygons with its own method's layers
and diameter — mixed-method assemblies carry every method's bodies in
one export. In the hierarchy, each die shows its method as a child row
whose properties view gives the per-method fragment provenance.

**L3 — Bodies inherit the interposer's render identity.** Being part of
the interposer mesh, the bodies share its render mode and selection, and
their colors resolve through the interposer technology rather than the
interconnect PDK's layer properties.

*Target:* a dedicated mesh group built from the fragment's layer keys,
giving the method its own render mode, 3D selection and `interconnect.lyp`
colors. Until then, per-layer show/hide is available through the
hierarchy's interconnect row and its properties view.

## What is deliberately decoupled already

- The two DRC adapter axes (interposer = where attachment lands,
  interconnect = how/at what density) and the manifest as single source
  of truth for methods.
- Per-die inline connection stacks: die seating is correct per die and
  vendor-agnostic.
- Discovery: fragments and the interconnect PDK resolve by environment
  variable or sibling-checkout walk; no fixed-depth paths.
- The `.chiplet` identity: `interconnect.technology` round-trips and the
  method is a first-class Technology in the UI.

## Running: PDK discovery in Docker vs host

The fragment lookup (`interconnectStackupFragmentPath`) resolves the
interconnect PDK as **env var first** (`INTERCONNECT_PDK_ROOT`), then a
sibling-checkout walk up from the `configs/` dir. This matters operationally:

- **Docker**: the `run-*.sh` launchers mount chiplet-studio at `/workspace`,
  which decouples it from the PDK tree, so the sibling walk cannot find the
  interconnect PDK. The env var is therefore required. The build image bakes
  it at a fixed container path (`docker/Dockerfile.build`:
  `INTERCONNECT_PDK_ROOT=/opt/pdks/interconnect_pdk`, plus the interposer and
  base-PDK roots), and the launchers mount the host PDK dirs onto those paths
  via `scripts/pdk-env.sh`. This keeps the image free of host-specific paths
  so it runs on any host; override the `HOST_*` vars in `pdk-env.sh` if your
  local checkouts live elsewhere. Without it, `mergeInterconnectFragments`
  returns 0 and the bodies never enter the interposer mesh (no log line
  `Merged N interconnect body layers ...`).
- **Host (no Docker)**: the image ENV does not apply — export
  `INTERCONNECT_PDK_ROOT` / `INTERPOSER_PDK_ROOT` / `PDK_ROOT` yourself, or
  rely on the sibling walk if your checkouts are laid out for it.

Per L3, the bodies are part of the interposer mesh, so they only render when
the **interposer** component is in a Detailed render mode.
