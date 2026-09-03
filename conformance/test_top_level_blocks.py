# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Executable gate for the top-level block grammar and its oracle.

The grammar is normative in docs/CHIPLET_FORMAT_SPEC.md ("Top-level block
grammar"): which run of lines belongs to which top-level key, decided on the raw
text. Flow rule 4 (a host re-emits a flow block it did not author byte for byte)
is defined in terms of it, so three implementations run the SAME oracle file
rather than being compared to each other: the merge splitter in the KiCad plugin,
the Python reference reader, and the C++ reference reader.

This file proves the oracle is well formed and that the Python reference obeys it.
The C++ side reads the same file from its own test binary.
"""
import json
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "conformance" / "fixtures"

sys.path.insert(0, str(ROOT / "reference" / "python"))
import chiplet_format_io as cfio  # noqa: E402

CASES = json.loads(
    (FIXTURES / "top_level_blocks_cases.json").read_text(encoding="utf-8"))

ACCEPT = CASES["key_lines"]["accept"]
REJECT = CASES["key_lines"]["reject"]
SPLITS = CASES["splits"]
REFUSE = CASES["refuse"]

#: A quoted key at column zero, in the spelling the refuse cases must carry.
_QUOTED_KEY = re.compile(r'^(?:"[^"]*"|\'[^\']*\'):(?:\s.*)?\Z')


# --- (a) the oracle itself -------------------------------------------------
def test_oracle_is_wellformed():
    assert ACCEPT and REJECT and SPLITS and REFUSE
    lines = [c["line"] for c in ACCEPT]
    assert len(lines) == len(set(lines))
    assert len(REJECT) == len(set(REJECT))
    assert not set(lines) & set(REJECT)
    for case in ACCEPT:
        assert case["key"], case
        # The key is what stands before the colon, so it must open the line.
        assert case["line"].startswith(case["key"] + ":"), case
    for line in ACCEPT + [{"line": x} for x in REJECT]:
        assert "\n" not in line["line"], line
    names = [c["name"] for c in SPLITS] + [c["name"] for c in REFUSE]
    assert len(names) == len(set(names)), "duplicate case name"
    docs = [c["doc"] for c in SPLITS] + [c["doc"] for c in REFUSE]
    assert len(docs) == len(set(docs)), "duplicate case document"
    # The two spellings this file exists for, pinned by name: the quoted key at
    # column zero (which a splitter silently mis-attributes) and a CRLF document
    # (which an implementation anchoring on ECMAScript's narrower dot gets wrong).
    assert '"flow":' in REJECT
    assert any("crlf" in c["name"] for c in SPLITS)


@pytest.mark.parametrize("case", SPLITS, ids=lambda c: c["name"])
def test_oracle_split_case_is_wellformed(case):
    keys = [b["key"] for b in case["blocks"]]
    assert len(keys) >= 2, "a split case with one key discriminates nothing"
    assert len(keys) == len(set(keys)), "a key appears twice in one case"
    assert case["doc"]
    for block in case["blocks"]:
        assert block["text"], block
        if block["key"]:
            assert block["text"].startswith(block["key"] + ":"), block
    # Lossless by construction: the slices tile the document. A repeated key is
    # the one exception (its runs are joined under the first occurrence), and
    # that case says so.
    if case.get("reconstructs", True):
        assert "".join(b["text"] for b in case["blocks"]) == case["doc"]


@pytest.mark.parametrize("case", REFUSE, ids=lambda c: c["name"])
def test_oracle_refuse_case_is_wellformed(case):
    assert case["doc"] and case["reason"]
    quoted = [ln for ln in case["doc"].split("\n") if _QUOTED_KEY.match(ln)]
    assert quoted, "a refuse case must carry a quoted key at column zero"


# --- (b) the Python reference against the oracle ---------------------------
@pytest.mark.parametrize("case", ACCEPT, ids=lambda c: c["line"])
def test_key_line_accepted(case):
    assert cfio.top_level_key(case["line"]) == case["key"]


@pytest.mark.parametrize("line", REJECT, ids=repr)
def test_key_line_rejected(line):
    assert cfio.top_level_key(line) is None


@pytest.mark.parametrize("case", SPLITS, ids=lambda c: c["name"])
def test_split_matches_the_oracle(case):
    blocks = cfio.top_level_blocks(case["doc"])
    assert list(blocks.items()) == [(b["key"], b["text"]) for b in case["blocks"]]


@pytest.mark.parametrize("case", SPLITS, ids=lambda c: c["name"])
def test_named_block_matches_the_oracle(case):
    for block in case["blocks"]:
        assert cfio.top_level_block(case["doc"], block["key"]) == block["text"]
    assert cfio.top_level_block(case["doc"], "no_such_key") is None


@pytest.mark.parametrize("case", REFUSE, ids=lambda c: c["name"])
def test_quoted_key_at_column_zero_is_refused(case):
    with pytest.raises(cfio.ChipletFormatError):
        cfio.top_level_blocks(case["doc"])
    with pytest.raises(cfio.ChipletFormatError):
        cfio.top_level_block(case["doc"], "flow")


def test_loads_does_not_split_and_is_therefore_not_bound_by_the_guard():
    # The ownership guard binds a host that SPLITS. loads() parses YAML, where a
    # quoted key is an ordinary key, so it keeps reading these documents; the
    # asymmetry is deliberate and is the reason the guard lives on the splitter.
    doc = REFUSE[0]["doc"]
    assert cfio.loads(doc)["assembly"]["name"] == "demo"
