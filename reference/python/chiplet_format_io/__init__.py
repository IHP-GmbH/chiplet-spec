# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""chiplet_format_io -- permissive reference reader/writer for the .chiplet format.

Apache-2.0. Dependency-clean: depends only on PyYAML. It deliberately does NOT
import ``pcbnew`` or ``klayout`` (or any GPL library), so it can be embedded in
tools under any license, open-source or proprietary.

This is an INDEPENDENT reference implementation of the format described in
``docs/CHIPLET_FORMAT_SPEC.md``. It is intentionally *not* the byte-exact writer
used inside the GPL host tools (the KiCad plugin / KiCad fork exporter): those are
locked to each other by a byte-exact parity gate. Output here is canonical YAML,
semantically equivalent, not byte-identical to those hosts.

Typical use::

    import chiplet_format_io as cfio
    assembly = cfio.load("design.chiplet")      # -> dict, validated
    assembly["assembly"]["name"] = "renamed"
    cfio.dump(assembly, "design.chiplet")

Version policy (docs/VERSION_POLICY.md). One rule governs every versioned
artifact in the format family: same major with a minor at or below the supported
one is accepted, a same-major higher minor is accepted with a warning, a
different major or a malformed value is refused, and PATCH is ignored.
:func:`check_format_version` applies it to a ``.chiplet``'s own
``format_version``; :func:`check_contract_version` applies it to any governed
sidecar (``io_pads.json``, ``pins.json``, the black-box padmap, the boundary
manifest, ``interconnect_methods.json``)::

    cfio.check_contract_version(doc["version"], "1.0", name="io_pads.json")

Two version constants, and they answer different questions.
:data:`SUPPORTED_FORMAT_VERSION` is the highest ``.chiplet`` ``format_version``
this reader was written for, i.e. a fact about documents. :data:`__version__` is
the release of the reader itself, and it is the value a VENDORED copy of this
file carries into the tool that embedded it. That is what makes byte identity
unnecessary: a consumer requires a reader release (``cfio.__version__``) and a
document version, and never has to hash a copied file to find out what it has.
"""
from __future__ import annotations

import re
import warnings
from typing import Any, Callable, Dict, Optional, Tuple

import yaml

__all__ = [
    "SUPPORTED_FORMAT_VERSION",
    "__version__",
    "ChipletFormatError",
    "ContractVersionError",
    "ContractVersionWarning",
    "FormatVersionWarning",
    "check_contract_version",
    "check_format_version",
    "loads",
    "load",
    "dumps",
    "dump",
    "validate",
    "PREAMBLE_KEY",
    "top_level_key",
    "top_level_blocks",
    "top_level_block",
]

#: The highest ``format_version`` this reference implementation was written for.
#: The on-disk baseline stays additive-stable at "1.0"; readers are tolerant of
#: a same-major higher minor (see :func:`check_format_version`), so this is a
#: single exported string constant, never re-derived from a bump.
SUPPORTED_FORMAT_VERSION = "1.0"

#: The release of THIS reader, and the value a vendored copy carries with it.
#: Distinct from :data:`SUPPORTED_FORMAT_VERSION`, which is about the documents:
#: one says what is on disk, the other says which reader is in the tree. It is the
#: distribution version too (``pyproject.toml`` reads it from here), so a consumer
#: that installed the package and one that vendored the file agree on the number.
#: Bumped under the same policy as everything else (docs/VERSION_POLICY.md).
__version__ = "1.1.0"


class ChipletFormatError(ValueError):
    """Raised when a .chiplet document is malformed or unsupported."""


class ContractVersionError(ChipletFormatError):
    """A governed artifact declares a version this consumer cannot read.

    Raised by :func:`check_contract_version` for a malformed version string or a
    different major. A subclass of :class:`ChipletFormatError` so a consumer that
    already catches that keeps working; catch this one to tell "the version is
    wrong" apart from "the document is wrong".
    """


class ContractVersionWarning(UserWarning):
    """A governed artifact declares a newer same-major minor than is supported.

    The artifact is still read (as the supported version, ignoring unknown
    additions), but a same-major higher minor may carry fields this consumer does
    not understand, so the event is surfaced.
    """


class FormatVersionWarning(ContractVersionWarning):
    """The .chiplet-specific spelling of :class:`ContractVersionWarning`.

    Kept as its own class because callers filter on it by name; a subclass so a
    consumer can catch the general case for every governed artifact at once.
    """


def _parse_version(fv: Any) -> Optional[Tuple[int, int]]:
    """Parse a ``format_version`` value into ``(major, minor)`` or ``None``.

    The spec says ``format_version`` MUST be a quoted ``"MAJOR.MINOR"`` string.
    An int/float is coerced through ``str()`` for back-compat (an unquoted
    ``1.0`` in YAML), which is exactly where Python and yaml-cpp can diverge
    (unquoted ``1.10`` becomes ``1.1`` under PyYAML, ``1.10`` under yaml-cpp);
    the divergence is pinned by a conformance fixture, not hidden here.
    """
    if isinstance(fv, bool):
        return None
    if isinstance(fv, (int, float)):
        fv = str(fv)
    if not isinstance(fv, str):
        return None
    parts = fv.split(".")
    if len(parts) != 2:
        return None
    # ASCII digits only, checked before int(). int() is far more permissive than
    # the spec: it accepts a sign, underscore separators, surrounding whitespace
    # and non-ASCII digits, so "+1.0", "1.0_0", "1. 0", " 1.0 " and "1.\u0660"
    # all parsed as (1, 0) here. The C++ reference never did (parse_version_parts
    # requires all_digits), so those five were a READER PARITY divergence, not a
    # tolerance anyone chose: the same document loaded in Python and threw in
    # C++. Digits-first makes this reader agree with the C++ one and with the
    # schema pattern, which were already in agreement.
    if not all(part.isascii() and part.isdigit() for part in parts):
        return None
    major, minor = int(parts[0]), int(parts[1])
    if major < 0 or minor < 0:
        return None
    return (major, minor)


_SUPPORTED_MAJOR, _SUPPORTED_MINOR = _parse_version(SUPPORTED_FORMAT_VERSION)  # type: ignore[misc]

#: Module-private warn-once state, keyed by the raw "MAJOR.MINOR" that fired.
#: Resettable so a test suite does not leak dedup state across cases.
_WARNED_VERSIONS: set = set()


def _reset_version_warnings() -> None:
    """Clear the warn-once dedup set (test hook)."""
    _WARNED_VERSIONS.clear()


def check_format_version(fv: Any, *,
                         on_warn: Optional[Callable[[str], None]] = None) -> str:
    """Apply the tolerant ``format_version`` policy; return the normalized version.

    Policy: missing or malformed -> :class:`ChipletFormatError`; a major other
    than the supported major (higher OR lower) -> :class:`ChipletFormatError`;
    same major, minor <= supported -> accept silently; same major, higher minor
    -> accept, warn once per distinct version via ``warnings.warn`` AND deliver
    every event (undeduped) to ``on_warn`` when supplied.
    """
    if fv is None:
        raise ChipletFormatError("missing required key: format_version")
    parsed = _parse_version(fv)
    if parsed is None:
        raise ChipletFormatError(
            f"malformed format_version {fv!r}; expected a quoted "
            f'"MAJOR.MINOR" string')
    major, minor = parsed
    if major != _SUPPORTED_MAJOR:
        raise ChipletFormatError(
            f"unsupported format_version {fv!r}; this reader supports major "
            f"{_SUPPORTED_MAJOR} (e.g. {SUPPORTED_FORMAT_VERSION!r})")
    normalized = f"{major}.{minor}"
    if minor > _SUPPORTED_MINOR:
        msg = (
            f"format_version {fv!r} is newer than this reader's "
            f"{SUPPORTED_FORMAT_VERSION!r} (same major {major}); reading it as "
            f"{SUPPORTED_FORMAT_VERSION!r} and ignoring unknown additions")
        if on_warn is not None:
            on_warn(msg)
        if normalized not in _WARNED_VERSIONS:
            # Record the key only AFTER the warning has been delivered. With the
            # add first, a host that escalates warnings to errors saw the first
            # read raise and every later read succeed silently: the same document
            # accepted or refused depending on who read it first. Now an
            # escalating host refuses every time, consistently, and a normal host
            # still sees the warning once.
            warnings.warn(msg, FormatVersionWarning, stacklevel=2)
            _WARNED_VERSIONS.add(normalized)
    return normalized


def _parse_contract_version(value: Any) -> Optional[Tuple[int, int]]:
    """Parse a governed-sidecar version into ``(major, minor)``; ``None`` if not.

    Accepts a quoted ``"MAJOR.MINOR"`` or ``"MAJOR.MINOR.PATCH"`` string. PATCH is
    parsed only to be discarded: a patch never changes what a consumer may
    assume, so it must never change a verdict either. Unlike
    :func:`_parse_version` (the ``.chiplet`` ``format_version``, which is
    MAJOR.MINOR only and is pinned that way by schemas/chiplet.schema.json), a
    non-string is NOT coerced: the sidecars are JSON, where an unquoted 1.10 is
    the number 1.1 with no way back, so a bare number is malformed here.
    """
    if not isinstance(value, str):
        return None
    # No .strip(): the sidecar schemas' version patterns admit no surrounding
    # whitespace, and a reader must not accept what the schema refuses (the
    # earlier exemption, "surrounding whitespace is not a version change", made
    # this parser laxer than both the schema and _parse_version; SPEC-2).
    parts = value.split(".")
    if len(parts) not in (2, 3):
        return None
    # ASCII digits only, for the same reason _parse_version checks them: int()
    # accepts a sign, underscores, surrounding whitespace and non-ASCII digits,
    # none of which the shared version pattern in every governed sidecar schema
    # allows. This path is the one every sidecar goes through, so a value the
    # schema rejects must not be a value this reader accepts.
    if not all(part.isascii() and part.isdigit() for part in parts):
        return None
    numbers = [int(p) for p in parts]
    if any(n < 0 for n in numbers):
        return None
    return (numbers[0], numbers[1])


def check_contract_version(value: Any, supported: str, *, name: str,
                           on_warn: Optional[Callable[[str], None]] = None) -> str:
    """Apply the version policy to any governed artifact; return "MAJOR.MINOR".

    One rule for every governed sidecar (docs/VERSION_POLICY.md), the same one
    :func:`check_format_version` applies to a ``.chiplet``: a quoted
    ``MAJOR.MINOR`` or ``MAJOR.MINOR.PATCH`` string; same major with a minor at or
    below ``supported`` accepted silently; same major with a HIGHER minor accepted
    with a warning (the artifact may carry additions this consumer does not
    understand); a different major, a missing value or a malformed one refused
    with :class:`ContractVersionError`; PATCH ignored throughout.

    ``name`` identifies the artifact in messages (e.g. ``"io_pads.json"``), and is
    part of the warn-once key so two sidecars never suppress each other's warning.
    ``on_warn`` receives every event undeduped; the default ``warnings`` channel is
    deduped per (name, version), matching :func:`check_format_version`.

    The point is that a consumer gates on the CONTRACT, not on byte identity with
    a vendored copy: an emitter that ships a compatible minor keeps working, and
    an incompatible major fails loudly at the boundary instead of half-parsing.
    """
    sup = _parse_contract_version(supported)
    if sup is None:
        raise ValueError(
            f"supported version {supported!r} for {name} is not a "
            f'"MAJOR.MINOR" string')
    sup_major, sup_minor = sup
    if value is None:
        raise ContractVersionError(f"{name}: missing required key: version")
    parsed = _parse_contract_version(value)
    if parsed is None:
        raise ContractVersionError(
            f"{name}: malformed version {value!r}; expected a quoted "
            f'"MAJOR.MINOR" or "MAJOR.MINOR.PATCH" string')
    major, minor = parsed
    if major != sup_major:
        raise ContractVersionError(
            f"{name}: unsupported version {value!r}; this consumer supports "
            f"major {sup_major} (e.g. {supported!r})")
    normalized = f"{major}.{minor}"
    if minor > sup_minor:
        msg = (
            f"{name}: version {value!r} is newer than the supported "
            f"{supported!r} (same major {major}); reading it as {supported!r} "
            f"and ignoring unknown additions")
        if on_warn is not None:
            on_warn(msg)
        key = (name, normalized)
        if key not in _WARNED_VERSIONS:
            # Same ordering rule as check_format_version: deliver, then record.
            warnings.warn(msg, ContractVersionWarning, stacklevel=2)
            _WARNED_VERSIONS.add(key)
    return normalized


def _apply_write_version(out: Dict[str, Any]) -> None:
    """Set ``out['format_version']`` per the passthrough writer rule (H-B crux).

    The stamped version must describe the bytes written. This is a *lossless*
    passthrough writer (it re-emits the whole dict, unknown keys included), so a
    same-major higher-minor input is PRESERVED; a missing/lower/equal version is
    stamped down to :data:`SUPPORTED_FORMAT_VERSION`. Warning re-emission is done
    by the validate pass in :func:`dumps`, not here. (Lossy writers -- the C++
    struct writer, the from-scratch KiCad exporter -- stamp SUPPORTED instead.)
    """
    parsed = _parse_version(out.get("format_version"))
    if parsed is not None and parsed[0] == _SUPPORTED_MAJOR \
            and parsed[1] > _SUPPORTED_MINOR:
        out["format_version"] = f"{parsed[0]}.{parsed[1]}"
    else:
        out["format_version"] = SUPPORTED_FORMAT_VERSION


def _validate(data: Dict[str, Any], *, allow_intermediate: bool,
              on_warn: Optional[Callable[[str], None]] = None) -> Dict[str, Any]:
    if not isinstance(data, dict):
        raise ChipletFormatError("top-level .chiplet document must be a mapping")

    check_format_version(data.get("format_version"), on_warn=on_warn)

    meta = data.get("_metadata") or {}
    if isinstance(meta, dict) and meta.get("finalize_required") and not allow_intermediate:
        raise ChipletFormatError(
            "this is an intermediate .chiplet (_metadata.finalize_required: true); "
            "run the finalizer (e.g. hyp_to_gds --update-chiplet-file) before "
            "loading, or pass allow_intermediate=True"
        )

    assembly = data.get("assembly")
    if not isinstance(assembly, dict):
        raise ChipletFormatError("missing or invalid 'assembly' section")
    if not assembly.get("name"):
        raise ChipletFormatError("assembly.name is required")

    techs = data.get("technologies")
    if techs is not None and not isinstance(techs, dict):
        raise ChipletFormatError("'technologies' must be a mapping")

    comps = data.get("components")
    if comps is not None:
        if not isinstance(comps, list):
            raise ChipletFormatError("'components' must be a list")
        for i, comp in enumerate(comps):
            if not isinstance(comp, dict):
                raise ChipletFormatError(f"component[{i}] must be a mapping")
            if not comp.get("id"):
                raise ChipletFormatError(f"component[{i}] missing required 'id'")
            if not comp.get("type"):
                raise ChipletFormatError(
                    f"component {comp.get('id')!r} missing required 'type'"
                )

    return data


def validate(data: Dict[str, Any], *, allow_intermediate: bool = False,
             on_warn: Optional[Callable[[str], None]] = None) -> Dict[str, Any]:
    """Validate a parsed .chiplet mapping in place; return it. Raises on error."""
    return _validate(data, allow_intermediate=allow_intermediate, on_warn=on_warn)


def loads(text: str, *, allow_intermediate: bool = False, validate: bool = True,
          on_warn: Optional[Callable[[str], None]] = None) -> Dict[str, Any]:
    """Parse a .chiplet document from a YAML string into a dict.

    Refuses a document with a REPEATED top-level key, on the source text, before
    anything is read out of it. That is not a style rule: PyYAML keeps the last
    value and yaml-cpp the first, so this reader and the C++ one would report
    different documents from one file, and nothing downstream, schema included,
    can tell. It is the only text-level refusal here; a quoted key at column zero
    makes a document unsplittable, not invalid, and is read normally (see
    :func:`top_level_blocks` and flow rule 1).
    """
    data = yaml.safe_load(text)
    if data is None:
        raise ChipletFormatError("empty .chiplet document")
    # One pass over the text. Cheap, and it is the only place the repeat is
    # visible: `data` above has already resolved it away.
    _scan_top_level(text)
    if validate:
        _validate(data, allow_intermediate=allow_intermediate, on_warn=on_warn)
    return data


def load(path, *, allow_intermediate: bool = False, validate: bool = True,
         on_warn: Optional[Callable[[str], None]] = None) -> Dict[str, Any]:
    """Read and parse a .chiplet file into a dict."""
    with open(path, "r", encoding="utf-8") as fh:
        return loads(fh.read(), allow_intermediate=allow_intermediate,
                     validate=validate, on_warn=on_warn)


def dumps(data: Dict[str, Any], *, validate: bool = True,
          on_warn: Optional[Callable[[str], None]] = None) -> str:
    """Serialize a .chiplet mapping to a canonical YAML string.

    Key order is preserved (insertion order). This is semantic, not byte-exact
    to the GPL host writers. This is a lossless passthrough writer: the stamped
    ``format_version`` describes the bytes written, so a same-major higher-minor
    input is preserved (and, when validating, re-warns), never silently stamped
    down. See :func:`_apply_write_version`.

    It writes a ``flow`` block from the parsed value, never from source bytes, so
    it does not implement flow rule 4 and never claimed to. The corollary is that
    it has no "the block has no slice" state to refuse on: the C++ reference,
    which does keep the source slice, refuses to write a document whose flow
    block the grammar could not delimit (``FlowSource::NotDelimitable``). A host
    that needs rule 4 keeps the original text beside the parsed document and
    writes the text; this function is not that host.
    """
    out = dict(data)
    if validate:
        # Writing an intermediate (finalize_required) document is legitimate.
        # This re-runs check_format_version, so a preserved higher minor re-warns
        # on write (deduped default channel; every event to on_warn).
        _validate(out, allow_intermediate=True, on_warn=on_warn)
    _apply_write_version(out)
    return yaml.safe_dump(
        out,
        sort_keys=False,
        default_flow_style=False,
        allow_unicode=True,
    )


def dump(data: Dict[str, Any], path, *, validate: bool = True,
         on_warn: Optional[Callable[[str], None]] = None) -> None:
    """Serialize a .chiplet mapping to a file."""
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(dumps(data, validate=validate, on_warn=on_warn))


# --- the top-level block grammar -------------------------------------------
#
# Flow rule 4 (docs/CHIPLET_FORMAT_SPEC.md) says a host re-emits a flow block it
# did not author BYTE FOR BYTE. That is a statement about text, and it needs one
# definition of where a block starts and ends, shared by every implementation:
# the merge splitter in the KiCad plugin, this reader, and the C++ reference.
# The definition is normative in the spec under "Top-level block grammar" and
# every implementation is measured against
# conformance/fixtures/top_level_blocks_cases.json, never against another
# implementation.
#
# No YAML is parsed here, on purpose. A parsed node tree cannot answer the
# question: an emitter re-quotes scalars by its own rules (a source '0755' comes
# back bare and is an integer to the next PyYAML reader) and drops comments,
# which in a hand-written flow carry the author's intent.

#: A top-level block starts at a KEY LINE: a bare key at column zero, optionally
#: followed by whitespace and a value or a comment. Anchored with \Z, never $:
#: in Python $ also matches before a trailing newline, so a $-anchored copy of
#: this expression reads a different grammar than an ECMA-262 one does (the
#: defect adapter_id_cases.json was written for). The portable spelling of \Z is
#: (?![\s\S]); an ECMAScript implementation writes [^\n] for the dot, which
#: there excludes CR as well as LF.
_KEY_LINE_RE = re.compile(r"^([A-Za-z0-9_][A-Za-z0-9_.-]*):(?:\s.*)?\Z")

#: A quoted key at column zero. Valid YAML, NOT a key line, and therefore a
#: document this module refuses to split: a splitter would attach the block to
#: the preceding key, whose owner regenerates it away on the next export. This is
#: the spelling a document-wide key-quoting emitter switch produces, which is why
#: validation rule 7 forbids one.
_QUOTED_KEY_LINE_RE = re.compile(
    r"""^(?:"(?:[^"\\]|\\.)*"|'(?:[^']|'')*'):(?:\s.*)?\Z""")

#: Key of the PREAMBLE bucket: the lines before the first key line (a leading
#: ``---``, a file header comment). They belong to no top-level key.
PREAMBLE_KEY = ""


def _iter_lines(text: str):
    """Yield each line of ``text`` WITH its terminator, splitting on LF only.

    Not ``str.splitlines()``: that also splits on CR, VT, FF, NEL, U+2028 and
    friends, so a document with one of those inside a quoted scalar grows a
    top-level block that is not in the file. A line ends at LF, full stop.
    """
    start, n = 0, len(text)
    while start < n:
        nl = text.find("\n", start)
        end = n if nl < 0 else nl + 1
        yield text[start:end]
        start = end


def _content(line: str) -> str:
    """The part of a line the grammar matches: no LF, one optional CR removed."""
    if line.endswith("\n"):
        line = line[:-1]
    if line.endswith("\r"):
        line = line[:-1]
    return line


def top_level_key(line_content: str) -> Optional[str]:
    """Return the top-level key ``line_content`` opens, or ``None``.

    ``line_content`` is ONE line with its terminator already removed (that is
    what the oracle's ``key_lines`` cases carry). Deliberately strict: a value
    with a trailing newline is not a line, and silently accepting one is the
    ``$``-anchor defect wearing a different hat.
    """
    match = _KEY_LINE_RE.match(line_content)
    return match.group(1) if match else None


def _scan_top_level(text: str) -> Tuple[Dict[str, str], Optional[str]]:
    """One pass over the source text: the blocks, and why they may be unusable.

    Returns ``(blocks, not_splittable)``. ``not_splittable`` is ``None`` for a
    document that can be split, and otherwise the reason it cannot be, which the
    splitting accessors raise and :func:`loads` ignores. The two are different
    verdicts: a quoted key at column zero leaves nobody able to say who owns
    those bytes, and that is a reason not to SPLIT or WRITE the document, not a
    reason to refuse to read it.

    Raises :class:`ChipletFormatError` for a REPEATED top-level key, which is a
    different thing again: that document is ill-formed and every caller here,
    :func:`loads` included, refuses it. PyYAML resolves a repeated key to the
    last value and yaml-cpp to the first, so no reading of it is conforming.
    """
    blocks: Dict[str, str] = {PREAMBLE_KEY: ""}
    current = PREAMBLE_KEY
    not_splittable: Optional[str] = None
    for number, line in enumerate(_iter_lines(text), start=1):
        content = _content(line)
        key = top_level_key(content)
        if key is not None:
            if key in blocks:
                raise ChipletFormatError(
                    f"line {number}: repeated top-level key {key!r}. A document "
                    f"names each top-level key once. PyYAML resolves a repeat "
                    f"to the LAST value and yaml-cpp to the FIRST, so two "
                    f"conforming readers read different documents from these "
                    f"bytes, and neither the schema nor a parsed node tree can "
                    f"see that it happened "
                    f"(docs/CHIPLET_FORMAT_SPEC.md, top-level block grammar).")
            current = key
            blocks[key] = ""
        elif not_splittable is None and _QUOTED_KEY_LINE_RE.match(content):
            not_splittable = (
                f"line {number}: quoted key at column zero ({content!r}). A "
                f"quoted key is valid YAML but does not start a top-level "
                f"block, so this document cannot be split without attaching "
                f"the block to the preceding key, where its owner drops it on "
                f"the next re-export. Emit bare keys and quote values "
                f"(docs/CHIPLET_FORMAT_SPEC.md, top-level block grammar).")
        blocks[current] += line
    if not blocks[PREAMBLE_KEY]:
        del blocks[PREAMBLE_KEY]
    return blocks, not_splittable


def top_level_blocks(text: str) -> Dict[str, str]:
    """Split a .chiplet document into its top-level blocks, in document order.

    Returns ``{key: exact source text}``. A block runs from its key line up to
    but excluding the next key line, or to end of file, and the text is the
    SOURCE BYTES: key line included, original line endings, no trailing-newline
    normalisation, nothing stripped. Lines before the first key line land under
    :data:`PREAMBLE_KEY`, which is present only when there are any.

    Raises :class:`ChipletFormatError` twice over, for two different reasons:

    * a quoted key at column zero. It is not a key line, so splitting would
      attach that block to the preceding key and lose it on the next re-export.
      The document is still valid and :func:`loads` still reads it; splitting is
      what cannot be done.
    * a repeated top-level key. That document is ill-formed and :func:`loads`
      refuses it too.
    """
    blocks, not_splittable = _scan_top_level(text)
    if not_splittable is not None:
        raise ChipletFormatError(not_splittable)
    return blocks


def top_level_block(text: str, key: str) -> Optional[str]:
    """Return the exact source text of one top-level block, or ``None``.

    Convenience over :func:`top_level_blocks` with the same guarantees, and the
    call flow rule 4 needs: ``top_level_block(text, "flow")`` is the block a host
    that did not author it re-emits unchanged.
    """
    return top_level_blocks(text).get(key)
