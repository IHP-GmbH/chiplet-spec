<!--
SPDX-License-Identifier: Apache-2.0
SPDX-FileCopyrightText: 2026 IHP GmbH
-->

# Version policy

One rule for every versioned artifact published from this repository: the
`.chiplet` `format_version` and each governed sidecar (`io_pads.json`,
`pins.json`, the black-box padmap, the boundary manifest,
`interconnect_methods.json`).

## The rule

A version is a **quoted** `MAJOR.MINOR` or `MAJOR.MINOR.PATCH` string. Given a
consumer that supports `MAJOR.MINOR`:

| Declared | Verdict |
|----------|---------|
| Same major, minor at or below supported | accept |
| Same major, higher minor | accept, and warn (see the channel note below) |
| Different major (higher or lower) | refuse |
| Missing or malformed | refuse |

`PATCH` is parsed and then ignored; it never changes a verdict.

Accepting a higher minor is what makes the format additive: a consumer reads the
document as the version it supports and ignores what it does not recognize, and
the warning is there so an unexplained missing field has somewhere to point.

**The warning channel, stated so no host is surprised.** The reference readers deliver the
same-major-higher-minor event two ways: through Python's `warnings` channel (category
`ContractVersionWarning`, or its `.chiplet` subclass `FormatVersionWarning`, deduplicated per
artifact and version) for a human to see, and, when the caller passes `on_warn`, to that hook,
every event, undeduplicated. The `warnings` channel is a process-global the HOST configures. A
host that escalates warnings to errors (`python -W error`, `filterwarnings("error")`, a test
runner set strict) will therefore REFUSE a compatible higher minor, and it does so by its own
choice, not by this policy. A consumer that needs policy-conformant acceptance under such a
host filters the category around the call or takes the event through `on_warn`, which never
escalates. The C++ reader has no global channel and returns the verdict; a C++ host decides.
This is the same statement as the rest of the policy: what is accepted is decided here, how
loudly is decided by the host, and the two must not be confused.
Refusing a *lower* major matters as much as refusing a higher one: an older major
is a different document shape, and half-parsing it is worse than not reading it.

Quoting is part of the rule, not a style preference. Unquoted `1.10` is `1.1`
under PyYAML and `1.10` under yaml-cpp, and in JSON it is the number `1.1` with
no way back.

The reference implementation is
`chiplet_format_io.check_contract_version(value, supported, name=...)`, where
`supported` is one `"MAJOR.MINOR"` string or a sequence of them (see [Changing
the major](#changing-the-major)), with `check_format_version` as the
`.chiplet`-specific entry point, reading the module's `ACCEPTED_FORMAT_VERSIONS`.
`SUPPORTED_FORMAT_VERSION` is the version writers stamp and must be a member of
that set, because a writer that stamps what its own reader refuses is a policy
nobody can follow; the C++ mirror carries the same pair (`ACCEPTED_FORMAT_VERSIONS`,
checked by a `static_assert`) and the same verdicts, and both run the shared
verdict oracle `conformance/fixtures/version_policy_cases.json`. One difference,
inherited rather than chosen: a `.chiplet` `format_version` is `MAJOR.MINOR` only
(no patch component), and `schemas/chiplet.schema.json` pins it that way; the
sidecars accept the patch component because emitters already write `"1.0.0"`.

## What bumps what

- **MINOR** for anything additive, *and for any change to the meaning of an
  existing key*. A key that silently starts meaning something else is the one
  change a consumer cannot detect, so it never happens under an unchanged
  version. If old and new readings of the same bytes can disagree, it is at least
  a minor.
- **MAJOR** for a change that makes an old consumer wrong rather than incomplete:
  removing a key, changing a type, changing a coordinate frame or a unit.
- **PATCH** for anything a consumer cannot observe: wording, examples,
  a description in a schema.

The dividing line, stated once because every future argument is a version of it:
a MINOR only adds what a consumer can ignore and remain correct, and whatever a
consumer must honour to stay correct is a MAJOR. It is a question about the
CHANGE, not about the diff. A new optional key a reader may skip is a minor; the
same key is a major the moment skipping it makes the reader wrong rather than
incomplete, and a new value in a closed vocabulary is a minor precisely because
a consumer that does not know the value refuses the document instead of
misreading it.

## Changing the major

A MAJOR bump is the change that makes an old consumer wrong rather than
incomplete, so it cannot ship the way a MINOR does. On the day a producer
switches, every consumer that has not switched refuses the document. The refusal
is correct; the flag day it creates is not necessary, and this section removes
the flag day without weakening the refusal.

**1. A consumer declares the SET of majors it accepts.** One `MAJOR.MINOR` entry
per major, carrying the minor that consumer was written for. The single-string
form is the one-element set, with exactly the verdicts it always had, so nothing
changes for a consumer that accepts one major. Two entries with the same major
are a PROGRAMMING error rather than a data error: two floors for one major have
no verdict between them. They are refused at call time (`ValueError` in Python,
`std::invalid_argument` in C++), not at read time on whatever document happens to
arrive first, so the mistake surfaces on the consumer's first call. An empty set
is refused the same way: a consumer that accepts nothing can read nothing, and
silently refusing every document would read as bad data.

**2. The verdict.** Pick the entry whose major equals the declared major.

| Declared, against the accepted set | Verdict |
|---|---|
| No entry with that major (higher or lower) | refuse, naming EVERY accepted major |
| Minor at or below that entry's minor | accept |
| Same major, higher minor | accept, and warn (the channel note above) |
| Missing or malformed | refuse |

Naming every accepted major is the part that is easy to get wrong: a refusal
that names one major while the consumer accepts two sends the producer to fix
the wrong end of the window. `PATCH` is still parsed and ignored, on the declared
version and on the entries alike, so `1.0` and `1.0.0` are the same floor.

**3. The bump, in three steps, each shippable on its own.**

1. Consumers add the new major to their set while producers still emit the old
   one. A consumer may add a major only when the code path for that major exists:
   "accept" is a statement about the reader, never an intention.
2. Producers switch to the new major. Both majors are now in the field, every
   updated consumer reads both, and the conformance corpus carries a document of
   each major for as long as the window is open.
3. Once every governed producer emits the new major, consumers drop the old
   entry and the corpus drops the old document.

The window has to be finite, and step 3 is what makes it so. A set that only
grows is a reader that still understands every shape the format ever had, which
is the state a major bump exists to leave.

## Deprecating a key

Announce it, keep it working for at least one minor, then remove it in the next
major. Concretely: bump MINOR when the replacement lands, keep emitting and
reading the old key for that minor with the schema description naming it
deprecated and naming the replacement, and only then stop. A key that disappears
in the same release that announces it is a major change wearing a minor's label.

The one deprecation currently in flight is `pins.json`, whose existing producers
write the bare integer `1` where the rule wants the string `"1.0"`. The schema
accepts both for now and names the integer deprecated; a consumer normalizes `1`
to `"1.0"` before applying the rule, and that normalization is the only place the
legacy spelling is understood. `check_contract_version` refuses a bare number,
because JSON cannot tell `1.0` from `1` and neither can the reader.

## Byte identity of vendored copies is a tripwire, not the contract

Several of these artifacts are vendored: the reference reader is copied into
consuming tools, and schemas are mirrored next to the code that reads them. It is
useful to notice when a copy has drifted, and a byte-comparison in CI is a fine
way to notice it.

It is not the compatibility contract. A copy that differs by a comment is
compatible; a copy that is byte-identical to an artifact whose *meaning* changed
is not. Consumers gate on the declared version through the rule above, never on
a hash of a vendored file. Where the vendored artifact is the reader itself,
`chiplet_format_io.__version__` is the value the copy carries for exactly this
purpose, so a consumer can require a reader release without comparing bytes. It
is the release of the reader, not of the format: `SUPPORTED_FORMAT_VERSION`
still says which documents that reader understands.

The C++ reference carries the same number in `READER_RELEASE`, so a vendored C++
copy is gateable the same way and neither reference can move alone: a
conformance test reads the constant out of the header, the version out of the
C++ package metadata, and `__version__` out of the module, and fails if the
three disagree.
