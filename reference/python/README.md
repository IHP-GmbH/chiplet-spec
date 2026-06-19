<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# chiplet-format-io (Python reference implementation)

Permissive (**Apache-2.0**) reader/writer for the `.chiplet` assembly format. Depends
only on **PyYAML** and deliberately imports no GPL library (`pcbnew`, `klayout`), so it
can be embedded in tools under any license, open-source or proprietary.

This is an **independent reference** implementation of `../../docs/CHIPLET_FORMAT_SPEC.md`.
It is intentionally not the byte-exact writer used inside the GPL host tools (the KiCad
plugin and the KiCad-fork exporter are locked to each other by a byte-exact parity gate);
output here is semantically equivalent canonical YAML, not byte-identical to those hosts.

## Quick start

The library works on plain Python `dict` objects: load, mutate, dump.

```python
import chiplet_format_io as cfio

assembly = cfio.load("design.chiplet")          # dict, validated
assembly["assembly"]["name"] = "renamed"
cfio.dump(assembly, "design.chiplet")           # canonical YAML
```

## Install / test

```bash
pip install -e .            # or: pip install PyYAML
pytest tests -q
```

The distribution name is `chiplet-format-io`; the import package is `chiplet_format_io`.

## API

All public names are exported from the top-level package.

| Name | Kind | Description |
| --- | --- | --- |
| `load(path, *, allow_intermediate=False, validate=True)` | function | Read a `.chiplet` file and return a `dict`. |
| `loads(text, *, allow_intermediate=False, validate=True)` | function | Parse a `.chiplet` document from a YAML string into a `dict`. |
| `dump(data, path, *, validate=True)` | function | Write a `dict` to a `.chiplet` file as canonical YAML. |
| `dumps(data, *, validate=True)` | function | Serialize a `dict` to a canonical YAML string. |
| `validate(data, *, allow_intermediate=False)` | function | Validate a parsed mapping in place; return it; raise on error. |
| `ChipletFormatError` | exception | Raised when a document is malformed or unsupported; subclass of `ValueError`. |
| `SUPPORTED_FORMAT_VERSION` | str | The only `format_version` this implementation understands (`"1.0"`). |

### Validation

Reads validate by default. `validate=True` checks the structural contract: `format_version`
must equal `SUPPORTED_FORMAT_VERSION`, the `assembly` section must exist and carry a non-empty
`name`, `technologies` (if present) must be a mapping, and each entry in `components` (if present)
must be a mapping with non-empty `id` and `type`. Failures raise `ChipletFormatError`. Pass
`validate=False` to skip these checks (for example, to inspect a document you already know is
nonconforming).

### Canonical YAML and key order

`dump` and `dumps` emit canonical YAML: block style, `sort_keys=False` so insertion order is
preserved, and `allow_unicode=True`. This is semantic, not byte-exact to the GPL host writers.

### Intermediate documents

A document carrying `_metadata.finalize_required: true` is an intermediate file that still needs
its finalizer (for example, `hyp_to_gds --update-chiplet-file`). On read, such a document is
refused by default; pass `allow_intermediate=True` to load it anyway. Writing an intermediate
document is always permitted (`dump`/`dumps` validate with intermediates allowed).
