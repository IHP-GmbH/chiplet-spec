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
NOT_DELIMITABLE = CASES["not_delimitable"]

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
    assert isinstance(case["loads"], bool)
    quoted = [ln for ln in case["doc"].split("\n") if _QUOTED_KEY.match(ln)]
    assert quoted, "a refuse case must carry a quoted key at column zero"
    # "writes" is a statement about a source-slice writer and only means anything
    # for a document a reader can hold in the first place.
    assert case["loads"] is True
    assert isinstance(case["writes"], bool)


@pytest.mark.parametrize("case", NOT_DELIMITABLE, ids=lambda c: c["name"])
def test_oracle_not_delimitable_case_is_wellformed(case):
    assert case["doc"] and case["reason"]
    # The section exists for documents that carry a flow node the grammar cannot
    # delimit, so a case without a flow node is not one of them...
    assert "flow" in case["doc"]
    # ...and neither is one whose flow IS a key line, which would have a slice.
    assert not any(cfio.top_level_key(line) == "flow"
                   for line in case["doc"].split("\n"))


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
def test_splitting_is_refused(case):
    with pytest.raises(cfio.ChipletFormatError):
        cfio.top_level_blocks(case["doc"])
    with pytest.raises(cfio.ChipletFormatError):
        cfio.top_level_block(case["doc"], "flow")


@pytest.mark.parametrize("case", REFUSE, ids=lambda c: c["name"])
def test_a_document_a_splitter_refuses_is_still_read(case):
    # Splitting and reading are different verdicts. A quoted key at column zero
    # is an ordinary key to YAML: the document is structurally valid, and flow
    # rule 1 says a reader that cannot handle the flow block MUST NOT reject the
    # file. Refusing here would make the reference reader stricter than the spec
    # it defines, over a question (who owns which bytes) that only a host writing
    # the file back has to answer.
    if not case["loads"]:
        pytest.skip("this document is ill-formed and refused at load")
    assert cfio.loads(case["doc"])["assembly"]["name"]


@pytest.mark.parametrize("case", NOT_DELIMITABLE, ids=lambda c: c["name"])
def test_a_flow_block_the_grammar_cannot_delimit_still_loads(case):
    # Same rule, the other spelling: the flow node is there, YAML reads it, and
    # the grammar has no slice for it. Loading is unaffected.
    assert cfio.loads(case["doc"])["flow"] is not None


@pytest.mark.parametrize("case", NOT_DELIMITABLE, ids=lambda c: c["name"])
def test_a_flow_block_the_grammar_cannot_delimit_splits_without_a_flow_key(case):
    # And the split succeeds; it just has no flow block to hand over. That is the
    # signal a source-slice writer refuses on (the C++ reference: flow_source
    # NotDelimitable, dumps() throws). This reader's dumps() re-emits from the
    # dict and never claimed byte-exactness, so there is nothing here to refuse.
    assert "flow" not in cfio.top_level_blocks(case["doc"])
