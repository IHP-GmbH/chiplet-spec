# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Tests for the chiplet_format_io reference reader/writer."""
import sys
from pathlib import Path

import pytest
import yaml

import chiplet_format_io as cfio

REPO_ROOT = Path(__file__).resolve().parents[3]
EXAMPLES = REPO_ROOT / "examples"


def test_roundtrip_canonical_example():
    """load -> dump -> load is a semantic fixed point on a real example."""
    path = EXAMPLES / "interposer_demo_design.chiplet"
    first = cfio.load(path)
    text = cfio.dumps(first)
    second = cfio.loads(text)
    assert first == second
    # key order preserved on the top level
    assert list(first.keys())[0] == "format_version"


def test_all_example_chiplets_parse():
    chiplets = list(EXAMPLES.glob("*.chiplet"))
    assert chiplets, "expected at least one example .chiplet"
    for p in chiplets:
        data = cfio.load(p, allow_intermediate=True)
        assert str(data["format_version"]) == cfio.SUPPORTED_FORMAT_VERSION


def test_missing_format_version_rejected():
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads("assembly:\n  name: x\n")


def test_unsupported_version_rejected():
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads('format_version: "2.0"\nassembly:\n  name: x\n')


def test_assembly_name_required():
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads('format_version: "1.0"\nassembly:\n  units: um\n')


def test_component_requires_id_and_type():
    doc = 'format_version: "1.0"\nassembly:\n  name: a\ncomponents:\n- type: die\n'
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads(doc)


def test_intermediate_refused_by_default_then_allowed():
    doc = (
        'format_version: "1.0"\n'
        "_metadata:\n  finalize_required: true\n"
        "assembly:\n  name: a\n"
    )
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads(doc)
    data = cfio.loads(doc, allow_intermediate=True)
    assert data["assembly"]["name"] == "a"


def test_dump_roundtrips_components_order():
    doc = cfio.load(EXAMPLES / "interposer_demo_design.chiplet")
    ids = [c["id"] for c in doc["components"]]
    reloaded = cfio.loads(cfio.dumps(doc))
    assert [c["id"] for c in reloaded["components"]] == ids


def _component(doc, cid):
    for c in doc["components"]:
        if c["id"] == cid:
            return c
    return None


def test_attachment_surface_z_roundtrip():
    """The interposer's die-mount plane parses, survives dump/load and is
    distinct from the physical body thickness; dies carry none (consumers fall
    back to dimensions.thickness)."""
    doc = cfio.load(EXAMPLES / "interposer_demo_design.chiplet")
    interposer = _component(doc, "interposer")
    assert interposer is not None
    assert interposer["attachment_surface_z"] == 13.83
    # thickness is now the physical body, decoupled from the mount plane.
    assert interposer["dimensions"]["thickness"] == 300.0

    die = _component(doc, "U1")
    assert die is not None
    assert "attachment_surface_z" not in die

    reloaded = cfio.loads(cfio.dumps(doc))
    assert _component(reloaded, "interposer")["attachment_surface_z"] == 13.83


def test_no_gpl_runtime_dependency():
    """Importing/using the library must not pull in pcbnew or klayout."""
    cfio.loads('format_version: "1.0"\nassembly:\n  name: a\n')
    assert "pcbnew" not in sys.modules
    assert "klayout" not in sys.modules


def test_source_has_no_gpl_imports():
    src = Path(cfio.__file__).read_text(encoding="utf-8")
    assert "import pcbnew" not in src
    assert "import klayout" not in src
    assert "from klayout" not in src


def test_a_forbidden_line_break_is_refused_before_the_yaml_parse():
    """NEL, U+2028 and U+2029 make a document ill-formed, wherever they sit.

    PyYAML implements YAML 1.1 and reads all three as line breaks; yaml-cpp
    implements YAML 1.2 and does not, so the same bytes are two documents. The
    refusal is this library's, not PyYAML's: on the smuggle shape below PyYAML
    does not raise at all, it silently returns a second top-level format_version.
    """
    for char, code_point in (("\u0085", "U+0085"), ("\u2028", "U+2028"),
                             ("\u2029", "U+2029")):
        smuggle = ('format_version: "1.0"\nassembly:\n  name: demo' + char
                   + 'format_version: "9.0"\ncomponents: []\n')
        # What PyYAML alone does with it, which is why the check is here.
        assert yaml.safe_load(smuggle)["format_version"] == "9.0"
        for validate in (True, False):
            with pytest.raises(cfio.ChipletFormatError) as excinfo:
                cfio.loads(smuggle, validate=validate)
            assert code_point in str(excinfo.value)
            assert "line 3" in str(excinfo.value)


def test_the_writer_escapes_a_forbidden_line_break():
    """dumps() must not write bytes loads() refuses, and must not lose the value.

    yaml.safe_dump(allow_unicode=True) writes all three raw into a single-quoted
    scalar and PyYAML folds them back on the next read, so the value did not
    survive its own round trip before this.
    """
    for char, escape in (("\u0085", "\\N"), ("\u2028", "\\L"),
                         ("\u2029", "\\P")):
        doc = {"format_version": "1.0",
               "assembly": {"name": "demo" + char + "x"}}
        text = cfio.dumps(doc)
        assert char not in text
        assert escape in text
        assert cfio.loads(text)["assembly"]["name"] == "demo" + char + "x"
