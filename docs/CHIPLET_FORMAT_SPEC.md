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

## File Structure

A `.chiplet` file contains four top-level sections:

```yaml
format_version: "1.0"

assembly:
  # Assembly metadata (required)

technologies:
  # Technology definitions (optional)

components:
  # Component list (optional)
```

---

## Root Level Keys

| Key | Required | Type | Description |
|-----|----------|------|-------------|
| `format_version` | **YES** | String | Format version, currently `"1.0"` |
| `assembly` | **YES** | Object | Assembly metadata |
| `technologies` | NO | Map | Technology definitions |
| `components` | NO | Array | List of components |

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
| `units` | NO | String | `"um"` | Unit of measurement |

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
| `layout` | NO | String | `""` | Path to GDS/OASIS layout file |
| `top_cell` | NO | String | `""` | Top cell name in layout |
| `position` | NO | Object | `{x:0, y:0, z:0}` | 3D position |
| `rotation` | NO | Object | `{z:0}` | Rotation angles |
| `dimensions` | NO | Object | `{width:0, height:0, thickness:0}` | Physical size |
| `metadata` | NO | Map | `{}` | Custom key-value pairs |
| `array` | NO | Object | - | Array config (die_array only) |

### Component Types

| Type | Description |
|------|-------------|
| `die` | Single integrated circuit die |
| `die_array` | Array of identical dies (e.g., HBM stack) |
| `interposer` | Silicon interposer connecting multiple dies |
| `substrate` | Package substrate or carrier |

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

1. `format_version` must be `"1.0"`
2. `assembly.name` is required and must not be empty
3. Component `id` values must be unique
4. Component `type` must be one of: `die`, `die_array`, `interposer`, `substrate`
5. If `technology` is specified, it must reference a defined technology ID
6. If `layout` is specified, the file must exist at the resolved path
7. If `layer_properties` is specified, the `.lyp` file must exist

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
    layout: "<PATH_TO_GDS>"
    top_cell: "<CELL_NAME>"
    dimensions:
      width: <WIDTH_UM>
      height: <HEIGHT_UM>
      thickness: <THICKNESS_UM>
    position:
      x: <X_UM>
      y: <Y_UM>
      z: <Z_UM>
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
