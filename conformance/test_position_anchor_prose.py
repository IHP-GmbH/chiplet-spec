# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Gate for SPEC-31: the spec's ``position`` prose must not outrank its contract.

``docs/CHIPLET_FORMAT_SPEC.md`` used to say, twice and without a condition, that
``position`` is the component's geometric center. That holds only for
``anchor: bbox_center``. ``docs/coord_frame_contract.md`` section 2.1, which the
very same section of the spec names as the source of truth for the anchor
convention, places a ``gds_origin`` component's own GDS (0, 0) at ``position``
with no extra centering. The two texts contradicted each other one paragraph
apart, and the wrong one was the one a reader meets first: a reader implemented
from the prose alone puts every ``gds_origin`` component half its own extent
away. That is not hypothetical, it was found and closed in a real consumer
(interposer-pnr's ``boundary_rect``).

What a green here does NOT cover (META-2): that any reader IMPLEMENTS the
contract correctly. This file compares two documents to each other and runs no
reader. It also cannot see a third document repeating the unconditional claim;
it gates the two sites that carried it.
"""

import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = (ROOT / "docs" / "CHIPLET_FORMAT_SPEC.md").read_text(encoding="utf-8")
CONTRACT = (ROOT / "docs" / "coord_frame_contract.md").read_text(encoding="utf-8")


def test_the_contract_still_says_what_the_spec_was_corrected_against():
    """The correction is only right while its authority says this.

    Verified at the level it could be false: a gate reading only the spec would
    stay green if the CONTRACT moved and left the spec correct about nothing.
    """
    assert "The `position:` value is added to the GDS-origin of the cell" in CONTRACT
    assert "no extra centering" in CONTRACT
    assert "`position:` places the bbox center" in CONTRACT


def test_the_spec_never_claims_the_geometric_centre_unconditionally():
    for sentence in ("component's **geometric center**, not its corner",
                     "3D position of the component's **geometric center**"):
        assert sentence not in SPEC, sentence


def test_both_position_sites_name_the_anchor_that_decides():
    # The field table entry.
    table = [ln for ln in SPEC.split("\n") if ln.startswith("| `position` |")]
    assert len(table) == 1, table
    row = table[0]
    assert "`anchor:`" in row and "bbox_center" in row and "gds_origin" in row

    # The normative Frame bullet.
    assert "The frame fixes the" in SPEC and "it does not fix which point" in SPEC
    frame = SPEC.split("- **Frame.**", 1)[1].split("- **`anchor:`**", 1)[0]
    for token in ("bbox_center", "gds_origin", "no extra centering"):
        assert token in frame, token
