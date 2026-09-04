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

#: The three kinds of refusal, kept apart because they are not the same verdict:
#: a quoted key at column zero is a valid document nobody can split; a repeated
#: top-level key is a document no reader may read AND no splitter may attribute;
#: a forbidden line break is a document no reader may read while the splitter has
#: a perfectly good answer for it (those bytes are not line breaks, so no block
#: starts there), which is why the grammar case for it stays under splits.
NOT_SPLITTABLE = [c for c in REFUSE if c["kind"] == "quoted_key_at_column_zero"]
ILL_FORMED = [c for c in REFUSE if c["kind"] == "repeated_top_level_key"]
FORBIDDEN_LINE_BREAK = [c for c in REFUSE if c["kind"] == "forbidden_line_break"]

#: The refusals a SPLITTER owes, which is not the same list as the refusals a
#: READER owes. Parametrizing the splitter tests over all of REFUSE would assert
#: that top_level_blocks refuses a document whose split is well defined.
SPLITTER_REFUSES = NOT_SPLITTABLE + ILL_FORMED

#: Every case that must be refused at LOAD, whatever a splitter does with it.
READER_REFUSES = ILL_FORMED + FORBIDDEN_LINE_BREAK

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
    # All three refusal kinds are present. A kind that quietly empties out takes
    # its whole parametrized test with it and leaves a green run behind.
    assert NOT_SPLITTABLE and ILL_FORMED and FORBIDDEN_LINE_BREAK
    assert (len(NOT_SPLITTABLE) + len(ILL_FORMED)
            + len(FORBIDDEN_LINE_BREAK)) == len(REFUSE)
    # Both shapes of the line-break disagreement, for each of the three code
    # points. One shape alone certifies one direction of a parity break that
    # runs both ways: the smuggle is refused by yaml-cpp and read by PyYAML, and
    # the plain-scalar one is read by yaml-cpp and refused by PyYAML.
    for code_point in ("U+0085", "U+2028", "U+2029"):
        shapes = [c["name"] for c in FORBIDDEN_LINE_BREAK
                  if c["code_point"] == code_point]
        assert len(shapes) == 2, code_point
        assert any(n.endswith("_smuggles_a_top_level_key") for n in shapes)
        assert any(n.endswith("_inside_a_plain_scalar") for n in shapes)


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
    # Lossless by construction: the slices tile the document, with no exception.
    # The one case that used to need one, a repeated top-level key whose two runs
    # were joined under the first occurrence, is a refuse case now.
    assert "".join(b["text"] for b in case["blocks"]) == case["doc"]
    assert "reconstructs" not in case


@pytest.mark.parametrize("case", REFUSE, ids=lambda c: c["name"])
def test_oracle_refuse_case_is_wellformed(case):
    assert case["doc"] and case["reason"]
    assert isinstance(case["loads"], bool)
    if case["kind"] == "quoted_key_at_column_zero":
        quoted = [ln for ln in case["doc"].split("\n") if _QUOTED_KEY.match(ln)]
        assert quoted, "the case must carry a quoted key at column zero"
        # Valid YAML, so it loads; "writes" is a statement about a source-slice
        # writer and only means anything for a document a reader can hold.
        assert case["loads"] is True
        assert isinstance(case["writes"], bool)
    elif case["kind"] == "repeated_top_level_key":
        keys = [k for k in (cfio.top_level_key(ln)
                            for ln in case["doc"].split("\n")) if k]
        assert len(keys) != len(set(keys)), "the case must repeat a key"
        assert case["loads"] is False
        assert "writes" not in case, "a document no reader holds is never written"
    elif case["kind"] == "forbidden_line_break":
        assert case["loads"] is False
        assert "writes" not in case, "a document no reader holds is never written"
        # The case has to carry the character it is about, and the reader has to
        # be able to quote it back: all three are invisible in an editor, so a
        # refusal that named neither the code point nor the line would send the
        # author looking at the wrong thing.
        char = chr(int(case["code_point"][2:], 16))
        assert char in case["doc"], case["name"]
        assert case["doc"].split("\n")[case["line"] - 1].count(char) == 1
    else:
        raise AssertionError("unknown refuse kind " + case["kind"])


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


@pytest.mark.parametrize("case", SPLITTER_REFUSES, ids=lambda c: c["name"])
def test_splitting_is_refused(case):
    with pytest.raises(cfio.ChipletFormatError):
        cfio.top_level_blocks(case["doc"])
    with pytest.raises(cfio.ChipletFormatError):
        cfio.top_level_block(case["doc"], "flow")


@pytest.mark.parametrize("case", READER_REFUSES, ids=lambda c: c["name"])
def test_an_ill_formed_document_is_refused_at_load(case):
    # The other half of the asymmetry. A repeated top-level key is not a question
    # of ownership that only a splitter has to answer: PyYAML keeps the LAST value
    # and yaml-cpp the FIRST, so the two reference readers would report different
    # documents from one file and nothing downstream could tell. There is no
    # conforming reading, so there is no reading.
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads(case["doc"])


@pytest.mark.parametrize("case", NOT_SPLITTABLE, ids=lambda c: c["name"])
def test_a_document_a_splitter_refuses_is_still_read(case):
    # Splitting and reading are different verdicts. A quoted key at column zero
    # is an ordinary key to YAML: the document is structurally valid, and flow
    # rule 1 says a reader that cannot handle the flow block MUST NOT reject the
    # file. Refusing here would make the reference reader stricter than the spec
    # it defines, over a question (who owns which bytes) that only a host writing
    # the file back has to answer.
    assert cfio.loads(case["doc"])["assembly"]["name"]


@pytest.mark.parametrize("case", NOT_DELIMITABLE, ids=lambda c: c["name"])
def test_a_flow_block_the_grammar_cannot_delimit_still_loads(case):
    # Same rule, the other spelling: the flow node is there, YAML reads it, and
    # the grammar has no slice for it. Loading is unaffected.
    assert cfio.loads(case["doc"])["flow"] is not None


@pytest.mark.parametrize("case", FORBIDDEN_LINE_BREAK, ids=lambda c: c["name"])
def test_a_forbidden_line_break_is_refused_with_a_text_level_reason(case):
    # The refusal has to be the FORMAT's, not a parser's. On the smuggle shape
    # PyYAML does not raise at all and on the plain-scalar shape it raises with a
    # scanner message that describes a simple key, so a test that only asserted
    # "something went wrong" would pass on half the cases for the wrong reason
    # and on the other half by accident.
    with pytest.raises(cfio.ChipletFormatError) as excinfo:
        cfio.loads(case["doc"])
    message = str(excinfo.value)
    assert case["code_point"] in message, message
    assert f"line {case['line']}" in message, message
    assert "LF and CRLF" in message, message
    # Same verdict with validation off: this is a fact about the bytes, and a
    # consumer that opts out of semantic validation has not opted out of it.
    with pytest.raises(cfio.ChipletFormatError):
        cfio.loads(case["doc"], validate=False)


@pytest.mark.parametrize(
    "case", [c for c in FORBIDDEN_LINE_BREAK
             if c["name"].endswith("_smuggles_a_top_level_key")],
    ids=lambda c: c["name"])
def test_the_smuggle_changes_a_value_and_not_the_shape(case):
    # What the refusal is for, stated as the property a consumer would have to
    # check otherwise. PyYAML reads the separator as a line break, so the second
    # format_version lands as a top-level key: the KEY LIST is identical to the
    # same document with the separator taken out, and only the VALUE changes.
    # A "no unexpected top-level keys" guard is therefore not a control for this,
    # which is the reason the guard the plugin already has did not see it.
    import yaml  # noqa: PLC0415  (only this test needs the raw parser)

    char = chr(int(case["code_point"][2:], 16))
    # The benign twin: the same document with the smuggled tail cut off the
    # assembly name, which is what an author reading the file in an editor sees.
    plain_doc = case["doc"].replace(char + 'format_version: "9.0"', "")
    smuggled = yaml.safe_load(case["doc"])
    plain = yaml.safe_load(plain_doc)
    assert list(smuggled) == list(plain)
    assert smuggled["format_version"] == "9.0"
    assert plain["format_version"] == "1.0"
    assert plain["assembly"]["name"] == "demo"
    # And the structural view, which is the one every consumer of the grammar
    # has, cannot see it: the LF-only scan reports one format_version block.
    blocks = cfio.top_level_blocks(case["doc"])
    assert list(blocks) == list(plain)


def test_the_grammar_still_does_not_break_a_line_on_a_separator():
    # The control the refusal must not swallow. The splitter's answer for these
    # bytes was never in doubt and has not changed: they are not line breaks, so
    # no block starts at one, and a splitter built on str.splitlines() grows a
    # `flow` block that is not in the file. The LOAD verdict moved; this one did
    # not, and the two are different questions about the same document.
    case = next(c for c in SPLITS if c["name"] ==
                "unicode_line_separator_inside_a_scalar_is_not_a_line_break")
    assert case["loadable"] is False
    blocks = cfio.top_level_blocks(case["doc"])
    assert list(blocks.items()) == [(b["key"], b["text"])
                                    for b in case["blocks"]]
    with pytest.raises(cfio.ChipletFormatError) as excinfo:
        cfio.loads(case["doc"])
    assert "U+2028" in str(excinfo.value)


@pytest.mark.parametrize("case", FORBIDDEN_LINE_BREAK, ids=lambda c: c["name"])
def test_the_writer_escapes_what_the_reader_refuses(case):
    # The writer rule, and it is not decoration: yaml.safe_dump(allow_unicode)
    # writes these three raw into a SINGLE-quoted scalar, and PyYAML then folds
    # its own output on the way back in, so the value did not survive a round
    # trip through the reference writer. Escaped in a double-quoted scalar it
    # does, and the bytes on disk are a document this reader still accepts.
    char = chr(int(case["code_point"][2:], 16))
    doc = {"format_version": "1.0", "assembly": {"name": "demo" + char + "x"}}
    text = cfio.dumps(doc)
    assert char not in text
    escape = {"U+0085": "\\N", "U+2028": "\\L", "U+2029": "\\P"}[
        case["code_point"]]
    assert escape in text
    assert cfio.loads(text)["assembly"]["name"] == "demo" + char + "x"


@pytest.mark.parametrize("case", NOT_DELIMITABLE, ids=lambda c: c["name"])
def test_a_flow_block_the_grammar_cannot_delimit_splits_without_a_flow_key(case):
    # And the split succeeds; it just has no flow block to hand over. That is the
    # signal a source-slice writer refuses on (the C++ reference: flow_source
    # NotDelimitable, dumps() throws). This reader's dumps() re-emits from the
    # dict and never claimed byte-exactness, so there is nothing here to refuse.
    assert "flow" not in cfio.top_level_blocks(case["doc"])
