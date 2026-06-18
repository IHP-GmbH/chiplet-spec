<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# chiplet-format-io (Python reference implementation)

Permissive (**Apache-2.0**) reader/writer for the `.chiplet` assembly format. Depends
only on **PyYAML** and deliberately imports no GPL library (`pcbnew`, `klayout`), so it
can be embedded in tools under any license — open-source or proprietary.

```python
import chiplet_format_io as cfio

assembly = cfio.load("design.chiplet")          # dict, validated
assembly["assembly"]["name"] = "renamed"
cfio.dump(assembly, "design.chiplet")           # canonical YAML
```

This is an **independent reference** implementation of `../../docs/CHIPLET_FORMAT_SPEC.md`.
It is intentionally not the byte-exact writer used inside the GPL host tools (the KiCad
plugin and the KiCad-fork exporter are locked to each other by a byte-exact parity gate);
output here is semantically equivalent canonical YAML.

## Install / test

```bash
pip install -e .            # or: pip install PyYAML
pytest tests -q
```
