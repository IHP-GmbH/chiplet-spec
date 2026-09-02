# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Executable gate for the one version policy (docs/VERSION_POLICY.md).

Two halves, because the policy has two halves.

The RULE half is the reference implementation,
``chiplet_format_io.check_contract_version``: same major with a minor at or below
supported accepted, same-major higher minor accepted with a warning, different
major or malformed refused, PATCH ignored. That is the whole of "which versions a
consumer accepts", and it lives in code, not in a schema.

The STRUCTURAL half is the five governed sidecar schemas. Each one's version field
is a quoted MAJOR.MINOR(.PATCH) string and nothing more: the schemas deliberately
do NOT enumerate accepted versions, so a same-major minor bump does not require
every consumer to ship a new schema first. A test that a different major still
validates structurally is therefore a positive, not an oversight; the refusal is
tested on the checker, above.
"""
import copy
import json
import sys
import warnings
from pathlib import Path

import pytest
from jsonschema.validators import validator_for

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "reference" / "python"))

import chiplet_format_io as cfio  # noqa: E402

SCHEMAS = ROOT / "schemas"
EXAMPLES = SCHEMAS / "examples"

#: The single pattern every governed sidecar uses for its version field. Written
#: out here rather than read from one of the schemas: this file is the place that
#: says what the pattern must be, so a schema that drifts fails instead of
#: redefining the expectation.
VERSION_PATTERN = r"^[0-9]+\.[0-9]+(\.[0-9]+)?$"

#: schema file -> the key that carries the contract version in it. The key is
#: spelled ``schema_version`` in the interconnect registry for historical reasons;
#: it is the same contract version, under the same policy.
VERSIONED_SIDECARS = {
    "io_pads.schema.json": "version",
    "pins.schema.json": "version",
    "blackbox_padmap.schema.json": "version",
    "boundary_manifest.schema.json": "version",
    "interconnect_methods.schema.json": "schema_version",
}


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _validator(schema: dict):
    cls = validator_for(schema)
    cls.check_schema(schema)
    return cls(schema)


def _valid(schema: dict, instance) -> bool:
    return _validator(schema).is_valid(instance)


def _version_subschema(schema_file: str) -> dict:
    """The version field's own subschema, as a standalone schema.

    Used for the two sidecars that have no committed example instance
    (boundary manifest, interconnect registry): building a full valid instance of
    those would pin far more of their shape than this test is about. For the three
    that do have examples, the full document is exercised as well, below.
    """
    schema = _load(SCHEMAS / schema_file)
    key = VERSIONED_SIDECARS[schema_file]
    sub = copy.deepcopy(schema["properties"][key])
    sub["$schema"] = schema["$schema"]
    return sub


@pytest.fixture(autouse=True)
def _no_leaked_warn_state():
    """The checker warns once per (name, version); do not leak that across cases."""
    cfio._reset_version_warnings()
    yield
    cfio._reset_version_warnings()


# --- the rule: accepted ----------------------------------------------------
@pytest.mark.parametrize("declared,expected", [
    ("1.0", "1.0"),
    ("1.0.0", "1.0"),      # existing emitters write this; PATCH is ignored
    ("1.0.7", "1.0"),      # any patch, same verdict
    ("1.1", "1.1"),        # at supported
    ("1.0.0 ", "1.0"),     # surrounding whitespace is not a version change
])
def test_same_major_at_or_below_supported_is_accepted(declared, expected):
    with warnings.catch_warnings():
        warnings.simplefilter("error")  # a silent accept must stay silent
        assert cfio.check_contract_version(
            declared, "1.1", name="io_pads.json") == expected


def test_patch_never_changes_the_verdict():
    # The point of ignoring PATCH: same answer with and without it.
    assert (cfio.check_contract_version("1.0", "1.0", name="pins.json")
            == cfio.check_contract_version("1.0.99", "1.0", name="pins.json"))


# --- the rule: higher minor accepted, with a warning -----------------------
def test_higher_minor_is_accepted_with_a_warning():
    with pytest.warns(cfio.ContractVersionWarning) as record:
        got = cfio.check_contract_version("1.4", "1.0", name="io_pads.json")
    assert got == "1.4"  # normalized, not clamped: the caller sees what it read
    assert "io_pads.json" in str(record[0].message)


def test_higher_minor_warning_reaches_the_on_warn_callback():
    seen = []
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        cfio.check_contract_version("1.4", "1.0", name="pins.json",
                                    on_warn=seen.append)
    assert len(seen) == 1 and "pins.json" in seen[0]


def test_the_warning_is_deduped_per_artifact_not_globally():
    # Two sidecars at the same newer minor must both be heard; one must not
    # suppress the other by sharing the warn-once key.
    with pytest.warns(cfio.ContractVersionWarning) as record:
        cfio.check_contract_version("1.4", "1.0", name="io_pads.json")
        cfio.check_contract_version("1.4", "1.0", name="pins.json")
        cfio.check_contract_version("1.4", "1.0", name="io_pads.json")  # deduped
    assert len(record) == 2


def test_format_version_warning_is_a_contract_version_warning():
    # A consumer that wants every governed artifact catches the general class;
    # the .chiplet-specific class stays catchable by name for existing filters.
    assert issubclass(cfio.FormatVersionWarning, cfio.ContractVersionWarning)


# --- the rule: refused -----------------------------------------------------
@pytest.mark.parametrize("declared", ["2.0", "2.0.0", "0.9", "10.0"])
def test_a_different_major_is_refused(declared):
    # Lower majors too: an older major is a different document shape, and
    # half-parsing it is worse than refusing it.
    with pytest.raises(cfio.ContractVersionError):
        cfio.check_contract_version(declared, "1.0", name="io_pads.json")


@pytest.mark.parametrize("declared", [
    None,            # missing key
    "",
    "1",             # MAJOR alone is not a version
    "1.",
    "1.0.0.0",
    "v1.0",
    "1.0-rc1",
    "one.zero",
    "-1.0",
    1,               # a JSON number is not a quoted version
    1.0,             # and 1.10 would already have become 1.1
    ["1", "0"],
    {"major": 1},
    True,
])
def test_a_malformed_version_is_refused(declared):
    with pytest.raises(cfio.ContractVersionError):
        cfio.check_contract_version(declared, "1.0", name="pins.json")


def test_the_refusal_names_the_artifact():
    with pytest.raises(cfio.ContractVersionError) as excinfo:
        cfio.check_contract_version("2.0.0", "1.0", name="interconnect_methods.json")
    assert "interconnect_methods.json" in str(excinfo.value)


def test_the_error_is_catchable_as_the_general_format_error():
    # A consumer that already catches ChipletFormatError keeps working.
    assert issubclass(cfio.ContractVersionError, cfio.ChipletFormatError)
    with pytest.raises(cfio.ChipletFormatError):
        cfio.check_contract_version("2.0", "1.0", name="io_pads.json")


def test_a_malformed_supported_argument_is_a_caller_bug_not_a_data_error():
    # Wrong-data raises ContractVersionError; wrong-call raises ValueError, so a
    # typo in a consumer never reads as "the file is bad".
    with pytest.raises(ValueError) as excinfo:
        cfio.check_contract_version("1.0", "1", name="io_pads.json")
    assert not isinstance(excinfo.value, cfio.ContractVersionError)


def test_the_legacy_pins_integer_must_be_normalized_by_the_consumer():
    # pins.schema.json still accepts the bare integer 1 (the legacy spelling of
    # "1.0", deprecated per docs/VERSION_POLICY.md). The checker does not: a JSON
    # number cannot carry a minor. A consumer reading a legacy pin list maps 1 to
    # "1.0" before applying the policy, and that mapping is the only place the
    # legacy spelling is understood.
    with pytest.raises(cfio.ContractVersionError):
        cfio.check_contract_version(1, "1.0", name="pins.json")
    assert cfio.check_contract_version("1.0", "1.0", name="pins.json") == "1.0"


# --- the structural half: one pattern, five schemas ------------------------
def test_every_governed_sidecar_uses_the_same_version_pattern():
    for schema_file, key in VERSIONED_SIDECARS.items():
        schema = _load(SCHEMAS / schema_file)
        prop = schema["properties"][key]
        # pins keeps a oneOf for its legacy integer spelling; the string branch
        # still carries the shared pattern.
        branches = prop.get("oneOf", [prop])
        patterns = [b.get("pattern") for b in branches if b.get("type") == "string"]
        assert patterns == [VERSION_PATTERN], schema_file
        text = json.dumps(prop)
        assert "VERSION_POLICY.md" in text, schema_file


@pytest.mark.parametrize("schema_file", sorted(VERSIONED_SIDECARS))
@pytest.mark.parametrize("declared", ["1.0", "1.0.0", "1.10", "2.0.0"])
def test_a_wellformed_version_string_is_structurally_valid(schema_file, declared):
    # Including a different major: the schema is structural, the refusal is the
    # checker's job. Enumerating versions in the schema would mean every consumer
    # needs a new schema before it may read a compatible minor bump.
    assert _valid(_version_subschema(schema_file), declared)


@pytest.mark.parametrize("schema_file", sorted(VERSIONED_SIDECARS))
@pytest.mark.parametrize("declared", ["1", "1.", "1.0.0.0", "v1.0", "1.0-rc1",
                                      "", "one.zero", [1, 0], {"major": 1}])
def test_a_malformed_version_is_structurally_rejected(schema_file, declared):
    assert not _valid(_version_subschema(schema_file), declared)


def test_only_pins_still_accepts_the_legacy_integer_spelling():
    for schema_file in VERSIONED_SIDECARS:
        accepted = _valid(_version_subschema(schema_file), 1)
        assert accepted == (schema_file == "pins.schema.json"), schema_file


def test_an_unquoted_number_is_only_readable_where_the_legacy_branch_survives():
    # One more reason the rule says QUOTED: JSON Schema calls 1.0 an integer
    # (draft-06 onward, zero fractional part), so the deprecated pins branch
    # cannot tell 1.0 from 1 and neither can a consumer. Every schema that has
    # only the string branch rejects it outright, which is the end state.
    for schema_file in VERSIONED_SIDECARS:
        accepted = _valid(_version_subschema(schema_file), 1.0)
        assert accepted == (schema_file == "pins.schema.json"), schema_file
    # The checker refuses the number under every spelling regardless.
    for number in (1, 1.0):
        with pytest.raises(cfio.ContractVersionError):
            cfio.check_contract_version(number, "1.0", name="pins.json")


# --- the structural half, on whole documents where an example exists -------
IO_PADS_SCHEMA = _load(SCHEMAS / "io_pads.schema.json")
PINS_SCHEMA = _load(SCHEMAS / "pins.schema.json")
PADMAP_SCHEMA = _load(SCHEMAS / "blackbox_padmap.schema.json")
IO_PADS_EXAMPLE = _load(EXAMPLES / "io_pads.board_absolute.example.json")
PINS_EXAMPLE = _load(EXAMPLES / "pins.example.json")
PADMAP_EXAMPLE = _load(EXAMPLES / "blackbox_padmap.example.json")


def test_the_committed_examples_still_declare_a_policy_conformant_version():
    # The tripwire for the change itself: the shipped emitters write "1.0.0" and
    # the integer 1, and both must survive it.
    assert _valid(IO_PADS_SCHEMA, IO_PADS_EXAMPLE)
    assert cfio.check_contract_version(
        IO_PADS_EXAMPLE["version"], "1.0", name="io_pads.json") == "1.0"
    assert _valid(PINS_SCHEMA, PINS_EXAMPLE)


@pytest.mark.parametrize("declared", ["1.1", "1.1.0", "2.0.0"])
def test_a_minor_or_major_bump_keeps_io_pads_documents_valid(declared):
    doc = copy.deepcopy(IO_PADS_EXAMPLE)
    doc["version"] = declared
    assert _valid(IO_PADS_SCHEMA, doc)


@pytest.mark.parametrize("declared", ["1", "1.0.0.0", "latest", 1, 1.0])
def test_a_malformed_version_rejects_the_whole_io_pads_document(declared):
    doc = copy.deepcopy(IO_PADS_EXAMPLE)
    doc["version"] = declared
    assert not _valid(IO_PADS_SCHEMA, doc)


@pytest.mark.parametrize("declared", ["1.0", "1.0.0", 1])
def test_pins_accepts_both_spellings_on_a_whole_document(declared):
    doc = copy.deepcopy(PINS_EXAMPLE)
    doc["version"] = declared
    assert _valid(PINS_SCHEMA, doc)


@pytest.mark.parametrize("declared", ["1", "v1", 2, 2.0])
def test_pins_rejects_a_malformed_or_unknown_legacy_version(declared):
    # The legacy integer branch stays pinned to 1: it is a deprecated spelling of
    # one specific version, not a second numeric version axis.
    doc = copy.deepcopy(PINS_EXAMPLE)
    doc["version"] = declared
    assert not _valid(PINS_SCHEMA, doc)


def test_the_padmap_version_is_optional_but_checked_when_present():
    # This artifact shipped unversioned; requiring a version would invalidate
    # every padmap already on disk. Absent means 1.0.
    doc = copy.deepcopy(PADMAP_EXAMPLE)
    doc.pop("version", None)
    assert _valid(PADMAP_SCHEMA, doc)
    doc["version"] = "1.0.0"
    assert _valid(PADMAP_SCHEMA, doc)
    doc["version"] = "1"
    assert not _valid(PADMAP_SCHEMA, doc)


# --- the vendored-reader identity ------------------------------------------
def test_both_version_constants_exist_and_parse():
    # The two answer different questions (which documents / which reader), so
    # both must be present and both must be readable by the same rule. A vendored
    # copy that carries neither leaves a consumer with only byte comparison.
    assert cfio.check_contract_version(
        cfio.SUPPORTED_FORMAT_VERSION, cfio.SUPPORTED_FORMAT_VERSION,
        name="SUPPORTED_FORMAT_VERSION") == "1.0"
    major_minor = cfio.check_contract_version(
        cfio.__version__, cfio.__version__, name="chiplet_format_io.__version__")
    assert major_minor == ".".join(cfio.__version__.split(".")[:2])
    assert len(cfio.__version__.split(".")) == 3  # readers ship a full MAJOR.MINOR.PATCH


def test_the_reader_release_is_exported():
    assert "__version__" in cfio.__all__
    assert "SUPPORTED_FORMAT_VERSION" in cfio.__all__


def test_the_installed_distribution_agrees_with_the_module():
    # pyproject.toml reads the version off the module, so an installed package
    # and a vendored copy of the file can never disagree. Skipped rather than
    # failed when the suite runs off the source tree with nothing installed.
    metadata = pytest.importorskip("importlib.metadata")
    try:
        installed = metadata.version("chiplet-format-io")
    except metadata.PackageNotFoundError:
        pytest.skip("chiplet-format-io is not installed in this environment")
    assert installed == cfio.__version__
