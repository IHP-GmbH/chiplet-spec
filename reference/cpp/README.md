<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->

# chiplet_format_io (C++ reference library)

A permissive, **Apache-2.0** reference reader/writer for the `.chiplet`
interchange format. It is the C++ twin of the Python
[`chiplet-format-io`](../python) library and implements the format described in
[`../../docs/CHIPLET_FORMAT_SPEC.md`](../../docs/CHIPLET_FORMAT_SPEC.md).

## Why it exists

The format is permissive on purpose: anyone may implement readers, writers and
tools for `.chiplet` under **any** license, open-source or proprietary. This
library makes that practical in C++ without dragging in the copyleft host tools.

- **Dependency-clean.** The only third-party dependency is
  [yaml-cpp](https://github.com/jbeder/yaml-cpp) (MIT). The library does **not**
  include any KLayout, Qt or pcbnew header, so it can be embedded in tooling
  under any license. A test asserts the sources stay free of those.
- **Plain structs.** `load()` returns a `ChipletDocument` tree of plain value
  structs (`include/chiplet_format_io/chiplet_format_io.hpp`). Paths are kept
  verbatim; the library never touches the filesystem to resolve them; that is
  the consumer's job.
- **Semantic, not byte-exact.** This is an independent reference. It is *not*
  the byte-exact writer used inside the GPL host tools (the KiCad plugin / KiCad
  fork exporter), which are locked to each other by a separate parity gate.
  Output here is canonical YAML, semantically equivalent, not byte-identical.

## API

```cpp
#include <chiplet_format_io/chiplet_format_io.hpp>
namespace cfio = chiplet_format_io;

cfio::ChipletDocument doc = cfio::load("design.chiplet");   // parse + validate
doc.assembly.name = "renamed";
cfio::dump(doc, "design.chiplet");                          // canonical YAML

std::string text = cfio::dumps(doc);                        // to string
cfio::ChipletDocument d = cfio::loads(text);                // from string
cfio::validate(doc);                                        // throws on error
```

`cfio::load`/`loads` take an optional `LoadOptions{allow_intermediate, validate}`;
`cfio::dump`/`dumps` take an optional `DumpOptions{validate}`. Errors are reported
by throwing `cfio::ChipletFormatError`.

Validation strictness matches the C++ host reader it can replace: missing
`component.id`/`type`, missing `interface.id`/`type`, an unknown interface type,
and a missing `net.name` are always rejected during parsing. `validate=false`
relaxes only the document-level gate, which checks a missing `format_version`
key, a wrong `format_version` value, the `finalize_required` intermediate guard,
a missing or non-map `assembly` section, and a missing `assembly.name`.

## Build & test

Requires CMake (>= 3.16), a C++17 compiler and `libyaml-cpp-dev`.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If yaml-cpp is unavailable on the host, build inside a container, e.g.:

```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ libyaml-cpp-dev make &&
  cmake -S . -B build && cmake --build build &&
  ctest --test-dir build --output-on-failure'
```
