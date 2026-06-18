<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# chiplet-spec

Open specifications for the heterogeneous-integration / chiplet-assembly interchange
formats developed at IHP — chiefly the **`.chiplet`** assembly format and the sidecar
manifests and vocabularies that travel with it.

This repository is the **canonical, neutral home** for the formats, deliberately kept
**separate from any implementing tool**. The reference tools that read and write these
formats (the KiCad fork, the Chiplet Studio viewer, the KiCad plugin) are GPL-licensed
because they link/derive from GPL software (KiCad, KLayout). **The formats themselves
are not**: they are published here under the permissive **Apache License 2.0** so that
anyone — open-source or commercial — can build independent implementations.

## What's here

| Path | Contents |
|------|----------|
| `docs/CHIPLET_FORMAT_SPEC.md` | The `.chiplet` YAML assembly format (v1.0). |
| `docs/coord_frame_contract.md` | Canonical coordinate frames for `.chiplet` positions. |
| `docs/interconnect_render_contract.md` | Stackup-fragment merge + 3D-body rendering contract. |
| `docs/adapter_contract.md` | Required inputs/parameters for interposer & interconnect adapters. |
| `schemas/*.schema.json` | Machine-readable JSON Schemas (boundary manifest, interconnect methods, layers, rule params, interconnect rules). |
| `schemas/chiplet_pads.json` | Pad-layer vocabulary for black-box chiplets (GDS 205/0, 205/25, 206/0). |
| `examples/` | Sample `.chiplet`, `*.boundaries.json`, and `interconnect_methods.json`. |

## License and implementer rights

All artifacts in this repository are licensed under the **Apache License 2.0**
(`LICENSE`; per-file declarations follow the [REUSE](https://reuse.software) convention
in `.reuse/dep5`).

In addition, IHP makes the following explicit grant regarding the **formats** described
here:

> Anyone may implement readers, writers, converters, or any other tools that consume or
> produce files in these formats, under **any license** of their choosing (open-source
> or proprietary, royalty-free). The format definitions are intended as open,
> royalty-free interchange specifications. **IHP asserts no patents over the formats
> themselves** and will not use patents it may hold to prevent conformant
> implementations. Apache-2.0's patent grant (Section 3) applies to the material in this
> repository.

This means a GPL constraint only ever comes from a *tool* you choose to build on (e.g.
embedding KiCad or KLayout), never from these formats.

## Status

Single source of truth for the formats. The implementing tool repositories will be
migrated to consume their schemas and spec docs from here; until that migration lands,
copies may still exist inside those repositories and this repository is authoritative.

Versioning is per-artifact (e.g. the `.chiplet` format carries `format_version`, the
manifests carry `version`/`schema_version`). Changes follow semantic-versioning intent:
additive fields are minor; removals/renames are major.
