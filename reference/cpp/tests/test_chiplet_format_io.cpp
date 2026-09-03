// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 IHP GmbH
//
// Tests for the chiplet_format_io C++ reference reader/writer. Dependency-clean:
// a tiny hand-rolled assert harness, no test framework. Mirrors the coverage of
// the Python reference tests (reference/python/tests/test_chiplet_format_io.py).

#include "chiplet_format_io/chiplet_format_io.hpp"

// yaml-cpp is used here only to read the JSON oracle (JSON is YAML), never to
// judge what the reader produced.
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace cfio = chiplet_format_io;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "  FAIL: " << what << "\n";
    }
}

// Run `fn`; pass the check iff it does NOT throw.
template <typename Fn>
void check_no_throw(Fn fn, const std::string& what) {
    ++g_checks;
    try {
        fn();
        return;
    } catch (const cfio::ChipletFormatError& e) {
        std::cerr << "  FAIL (unexpected throw): " << what << ": " << e.what()
                  << "\n";
    } catch (...) {
        std::cerr << "  FAIL (unexpected throw): " << what << "\n";
    }
    ++g_failures;
}

// Run `fn`; pass the check iff it throws ChipletFormatError.
template <typename Fn>
void check_throws(Fn fn, const std::string& what) {
    ++g_checks;
    bool threw = false;
    try {
        fn();
    } catch (const cfio::ChipletFormatError&) {
        threw = true;
    } catch (...) {
        // wrong exception type
    }
    if (!threw) {
        ++g_failures;
        std::cerr << "  FAIL (expected throw): " << what << "\n";
    }
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const std::string kExamplesDir = CHIPLET_EXAMPLES_DIR;
const std::string kHeaderFile = CHIPLET_HEADER_FILE;
const std::string kSourceFile = CHIPLET_SOURCE_FILE;

// Where the shared top-level block grammar oracle lives. CMake derives a default
// from this source tree; the CHIPLET_BLOCK_ORACLE environment variable overrides
// it at RUN time, which is what lets a binary built in one checkout be measured
// against another checkout's oracle instead of the absolute path baked into it.
std::string oracle_path() {
    const char* env = std::getenv("CHIPLET_BLOCK_ORACLE");
    if (env != nullptr && *env != '\0') return std::string(env);
    return std::string(CHIPLET_BLOCK_ORACLE_DEFAULT);
}

// The oracle, parsed once. Reached only after main() has established that the
// file is there: an absent oracle must FAIL and name the path, never let the
// grammar tests report green over cases they never read.
const YAML::Node& block_oracle() {
    static const YAML::Node node = YAML::LoadFile(oracle_path());
    return node;
}

// The version policy's verdict oracle, on the same terms: a compile-time default
// derived from this source tree, overridable at run time with
// CHIPLET_VERSION_ORACLE, and checked for existence in main() before any case
// runs. JSON is a subset of YAML, so yaml-cpp reads the file the Python side
// reads with json.load.
std::string version_oracle_path() {
    const char* env = std::getenv("CHIPLET_VERSION_ORACLE");
    if (env != nullptr && *env != '\0') return std::string(env);
    return std::string(CHIPLET_VERSION_ORACLE_DEFAULT);
}

const YAML::Node& version_oracle() {
    static const YAML::Node node = YAML::LoadFile(version_oracle_path());
    return node;
}

void test_roundtrip_canonical_example() {
    const std::string path = kExamplesDir + "/interposer_demo_design.chiplet";
    cfio::ChipletDocument first = cfio::load(path);
    check(first.format_version == cfio::SUPPORTED_FORMAT_VERSION,
          "example format_version is 1.0");

    // dump is idempotent after the first normalization (the C++ analog of the
    // Python dict round-trip equality).
    const std::string d1 = cfio::dumps(first);
    cfio::ChipletDocument second = cfio::loads(d1);
    const std::string d2 = cfio::dumps(second);
    check(d1 == d2, "load->dump is a semantic fixed point on the example");
}

void test_all_example_chiplets_parse() {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(kExamplesDir)) {
        if (entry.path().extension() == ".chiplet") {
            ++count;
            cfio::LoadOptions opts;
            opts.allow_intermediate = true;
            cfio::ChipletDocument doc = cfio::load(entry.path().string(), opts);
            check(doc.format_version == cfio::SUPPORTED_FORMAT_VERSION,
                  "example " + entry.path().filename().string() + " is v1.0");
        }
    }
    check(count > 0, "found at least one example .chiplet");
}

void test_missing_format_version_rejected() {
    check_throws([] { cfio::loads("assembly:\n  name: x\n"); },
                 "missing format_version rejected");
}

void test_unsupported_version_rejected() {
    check_throws(
        [] { cfio::loads("format_version: \"2.0\"\nassembly:\n  name: x\n"); },
        "unsupported format_version rejected");
}

void test_assembly_name_required() {
    check_throws(
        [] { cfio::loads("format_version: \"1.0\"\nassembly:\n  units: um\n"); },
        "assembly.name required");
}

void test_component_requires_id_and_type() {
    check_throws(
        [] {
            cfio::loads(
                "format_version: \"1.0\"\nassembly:\n  name: a\n"
                "components:\n- type: die\n");
        },
        "component without id rejected");
}

void test_intermediate_refused_then_allowed() {
    const std::string doc =
        "format_version: \"1.0\"\n"
        "_metadata:\n  finalize_required: true\n"
        "assembly:\n  name: a\n";
    check_throws([&] { cfio::loads(doc); },
                 "intermediate refused by default");
    cfio::LoadOptions opts;
    opts.allow_intermediate = true;
    cfio::ChipletDocument parsed = cfio::loads(doc, opts);
    check(parsed.assembly.name == "a", "intermediate accepted with flag");
    check(parsed.metadata.finalize_required, "finalize_required preserved");
}

void test_dump_roundtrips_components_order() {
    const std::string path = kExamplesDir + "/interposer_demo_design.chiplet";
    cfio::ChipletDocument doc = cfio::load(path);
    std::vector<std::string> ids;
    for (const auto& c : doc.components) ids.push_back(c.id);
    cfio::ChipletDocument reloaded = cfio::loads(cfio::dumps(doc));
    std::vector<std::string> ids2;
    for (const auto& c : reloaded.components) ids2.push_back(c.id);
    check(ids == ids2, "component order preserved across dump/load");
    check(ids.size() >= 2, "example has the expected components");
}

void test_unknown_interface_type_rejected() {
    const std::string doc =
        "format_version: \"1.0\"\nassembly:\n  name: a\n"
        "interfaces:\n- id: i1\n  type: bogus_bond\n";
    check_throws([&] { cfio::loads(doc); }, "unknown interface type rejected");
}

// One document per member of the closed vocabulary of validation rule 4. That
// the four copies of the LIST agree (this reader, the schema, the spec prose,
// the Python constant) is checked by conformance/test_interface_types.py on the
// text; what a text comparison cannot show is that the reader actually accepts
// each member, which is this. solder_bump is the member added by SPEC-23.
void test_every_known_interface_type_is_accepted() {
    for (const std::string& type : {std::string("micro_bump"),
                                    std::string("copper_pillar"),
                                    std::string("tsv"),
                                    std::string("wire_bond"),
                                    std::string("solder_bump")}) {
        const std::string doc =
            "format_version: \"1.0\"\nassembly:\n  name: a\n"
            "interfaces:\n- id: i1\n  type: " + type + "\n";
        check_no_throw([&] { cfio::loads(doc); },
                       "interface type " + type + " accepted");
    }
}

// The flow block is the exact source slice, which is what flow rule 4 needs and
// what the header has always promised. The predecessor of these tests asserted
// !flow_yaml.empty() under the label "captured verbatim", which cannot tell a
// re-serialisation from the source text: it passed just as happily when the
// field held YAML::Dump(root["flow"]), comments gone and scalars re-quoted.
//
// The cases come from conformance/fixtures/top_level_blocks_cases.json, the same
// file the Python reference and the KiCad plugin's merge splitter run, so the
// three implementations are measured against one oracle and never against each
// other.
void test_flow_block_is_the_exact_source_slice() {
    const YAML::Node& oracle = block_oracle();
    int cases = 0;
    for (const auto& c : oracle["splits"]) {
        // A splitter-only document (a repeated top-level key) is not one a
        // reader is required to load.
        if (c["loadable"] && !c["loadable"].as<bool>()) continue;
        const std::string name = c["name"].as<std::string>();
        std::string expected;
        bool has_flow = false;
        for (const auto& b : c["blocks"]) {
            if (b["key"].as<std::string>() == "flow") {
                expected = b["text"].as<std::string>();
                has_flow = true;
            }
        }
        if (!has_flow) continue;
        ++cases;
        cfio::ChipletDocument parsed = cfio::loads(c["doc"].as<std::string>());
        check(parsed.has_flow, name + ": flow block detected");
        check(parsed.flow_source == cfio::FlowSource::Slice,
              name + ": the flow bytes are recorded as a source slice");
        check(parsed.flow_yaml == expected,
              name + ": flow_yaml is the source slice, byte for byte");
    }
    check(cases >= 5, "the oracle still carries the flow split cases");
}

// Rule 4 end to end: a document this writer did not author goes out with the
// same bytes it came in with. A node dump fails this on the comment alone.
void test_flow_block_is_re_emitted_byte_for_byte() {
    const YAML::Node& oracle = block_oracle();
    const std::string doc =
        oracle["splits"][0]["doc"].as<std::string>();
    cfio::ChipletDocument parsed = cfio::loads(doc);
    const std::string written = cfio::dumps(parsed);
    check(written.find(parsed.flow_yaml) != std::string::npos,
          "dumps() writes the flow slice verbatim");
    cfio::ChipletDocument reloaded = cfio::loads(written);
    check(reloaded.has_flow, "flow block survives dump/load");
    check(reloaded.flow_yaml == parsed.flow_yaml,
          "flow block round-trips byte for byte");
    check(parsed.flow_yaml.find("# export first") != std::string::npos,
          "the case under test carries the comment a node dump would drop");
}

// A quoted key at column zero is valid YAML and is NOT a top-level key line, so
// a splitter would hand the block to the preceding key, whose owner regenerates
// it away on the next export. That makes the document NOT SPLITTABLE, which is
// not the same as invalid: flow rule 1 says a reader must not reject a document
// over a block it cannot handle, so it LOADS. The guarantee that actually breaks
// is the write one, and that is where it is charged.
void test_quoted_key_at_column_zero_loads_and_may_refuse_to_write() {
    const YAML::Node& oracle = block_oracle();
    int cases = 0;
    for (const auto& c : oracle["refuse"]) {
        const std::string name = c["name"].as<std::string>();
        const std::string doc = c["doc"].as<std::string>();
        if (!c["loads"].as<bool>()) {
            check_throws([&] { cfio::loads(doc); }, name + ": refused at load");
            continue;
        }
        ++cases;
        check_no_throw([&] { cfio::loads(doc); }, name + ": still loads");
        cfio::ChipletDocument parsed = cfio::loads(doc);
        check(!parsed.assembly.name.empty(), name + ": and is read normally");
        if (c["writes"].as<bool>()) {
            check_no_throw([&] { cfio::dumps(parsed); },
                           name + ": no flow block, so it writes back");
        } else {
            check(parsed.flow_source == cfio::FlowSource::NotDelimitable,
                  name + ": its flow block has no source slice");
            check(parsed.flow_yaml.empty(), name + ": and no invented bytes");
            check_throws([&] { cfio::dumps(parsed); },
                         name + ": refuses to write the flow block back");
        }
    }
    check(cases >= 3, "the oracle still carries the refuse cases");
}

// A flow block the grammar cannot delimit: a flow-style document, or a key line
// spelled `flow :`, which YAML reads as the key `flow` and the grammar does not
// see at all. Either way the block has no slice, so re-emitting it byte for byte
// is impossible. The document still LOADS (flow rule 1), the missing slice is
// recorded rather than papered over, and the refusal lands on dumps(): the two
// alternatives there are dropping the block and emitting a node dump in the
// place the source text belongs, and both are silent lies.
void test_flow_block_the_grammar_cannot_delimit_loads_but_does_not_write() {
    const YAML::Node& oracle = block_oracle();
    int cases = 0;
    for (const auto& c : oracle["not_delimitable"]) {
        ++cases;
        const std::string name = c["name"].as<std::string>();
        const std::string doc = c["doc"].as<std::string>();
        check_no_throw([&] { cfio::loads(doc); }, name + ": loads");
        cfio::ChipletDocument parsed = cfio::loads(doc);
        check(parsed.has_flow, name + ": the flow node is seen");
        check(parsed.flow_source == cfio::FlowSource::NotDelimitable,
              name + ": recorded as having no source slice");
        check(parsed.flow_yaml.empty(), name + ": no invented bytes");
        check_throws([&] { cfio::dumps(parsed); },
                     name + ": refuses to write it back");

        // The way out the refusal names: the host re-authors the block, and now
        // owns the bytes it is asking to have written. A refusal with no way out
        // is a document a host can open and never save.
        cfio::ChipletDocument reauthored = parsed;
        reauthored.flow_yaml = "flow:\n  steps: []\n";
        check_no_throw([&] { cfio::dumps(reauthored); },
                       name + ": saves once the host re-authors the flow");
        cfio::ChipletDocument reloaded = cfio::loads(cfio::dumps(reauthored));
        check(reloaded.has_flow &&
                  reloaded.flow_source == cfio::FlowSource::Slice,
              name + ": and the saved file has a delimitable flow block");
    }
    check(cases >= 2, "the oracle still carries the not_delimitable cases");

    // Without a flow block, both spellings of the same document are fine: the
    // grammar only has to delimit what is there.
    cfio::ChipletDocument parsed =
        cfio::loads("{format_version: \"1.0\", assembly: {name: a}}\n");
    check(!parsed.has_flow && parsed.assembly.name == "a",
          "a flow-style document without a flow block still loads");
    check(parsed.flow_source == cfio::FlowSource::Absent,
          "and reports no flow source at all");
    check_no_throw([&] { cfio::dumps(parsed); },
                   "and writes back without complaint");
}

// A document assembled in memory, not read from a file, may still carry the
// pre-slice spelling: the flow VALUE with no key line. dumps() wraps that
// through the node tree so the output keeps a `flow:` key. Lossy, which is why
// the reader no longer produces it, but never silently dropped.
void test_hand_built_flow_value_without_a_key_line_still_emits() {
    cfio::ChipletDocument doc;
    doc.format_version = cfio::SUPPORTED_FORMAT_VERSION;
    doc.assembly.name = "a";
    doc.has_flow = true;
    doc.flow_yaml = "steps:\n  - name: export\n";
    cfio::ChipletDocument reloaded = cfio::loads(cfio::dumps(doc));
    check(reloaded.has_flow, "hand-built flow value survives dump/load");
    check(reloaded.flow_yaml.rfind("flow:", 0) == 0,
          "and comes back as a proper flow block, key line included");
}

// Validation rule 8: a pad's io_class must allow the type of the interface it
// takes part in, and the pad set of an endpoint is scoped by port_layer. The
// TABLE is compared with the spec and with the Python constant by
// conformance/test_pad_usage_compatibility.py; what only a run can show is the
// reader's behaviour, which is here. Documents are hand-built rather than read
// from the corpus: a fixture is a document, not the specification.
std::string pad_usage_doc(const std::string& io_class,
                          const std::string& pad_layer,
                          const std::string& iface_type,
                          const std::string& port_layer) {
    return "format_version: \"1.0\"\nassembly:\n  name: rule 8\n"
           "components:\n"
           "- id: interposer\n  type: interposer\n  io_pads:\n"
           "  - id: P1\n    io_class: " + io_class + "\n"
           "    position: {x: 0.0, y: 0.0}\n    layer: " + pad_layer + "\n"
           "- id: U1\n  type: die\n"
           "interfaces:\n- id: link0\n  type: " + iface_type + "\n"
           "  from: {component: U1, port_layer: " + port_layer + "}\n"
           "  to: {component: interposer, port_layer: " + port_layer + "}\n";
}

void test_pad_usage_rule_refuses_a_mismatched_pad() {
    check_throws([] {
        cfio::loads(pad_usage_doc("wire_bond", "TopMetal2", "copper_pillar",
                                  "TopMetal2"));
    }, "a wire_bond pad under a copper_pillar interface is refused");

    // The same pad and the same interface, one layer apart: the endpoint's pad
    // set is scoped by port_layer, so this one is not in it.
    check_no_throw([] {
        cfio::loads(pad_usage_doc("wire_bond", "Metal4", "copper_pillar",
                                  "TopMetal2"));
    }, "a mismatched pad on another layer is not in the pad set");

    // A row of the table, positive.
    check_no_throw([] {
        cfio::loads(pad_usage_doc("flipped_bump", "TopMetal2", "copper_pillar",
                                  "TopMetal2"));
    }, "a flipped_bump pad under a copper_pillar interface is accepted");

    // solder_bump is in the flipped_bump row, which is the SPEC-23 member
    // meeting the SPEC-22 table.
    check_no_throw([] {
        cfio::loads(pad_usage_doc("flipped_bump", "TopMetal2", "solder_bump",
                                  "TopMetal2"));
    }, "a flipped_bump pad under a solder_bump interface is accepted");

    // An endpoint whose component carries no inline pads (every die endpoint)
    // is out of scope by decision, until SPEC-24 gives interfaces a pad binding.
    check_no_throw([] {
        cfio::loads("format_version: \"1.0\"\nassembly:\n  name: a\n"
                    "components:\n- id: U1\n  type: die\n"
                    "interfaces:\n- id: link0\n  type: copper_pillar\n"
                    "  from: {component: U1, port_layer: TopMetal2}\n");
    }, "a die endpoint carries no pads and is not checked");

    // The refusal names what a designer needs to act on.
    bool named = false;
    try {
        cfio::loads(pad_usage_doc("wire_bond", "TopMetal2", "copper_pillar",
                                  "TopMetal2"));
    } catch (const cfio::ChipletFormatError& e) {
        const std::string what = e.what();
        named = what.find("link0") != std::string::npos &&
                what.find("P1") != std::string::npos &&
                what.find("wire_bond") != std::string::npos &&
                what.find("copper_pillar") != std::string::npos &&
                what.find("rule 8") != std::string::npos;
    }
    check(named, "the rule 8 refusal names the interface, the pad, the class "
                 "and the type");

    // And the writer will not emit one either: validate() carries the rule, so a
    // document built in memory cannot be saved into the corpus.
    cfio::ChipletDocument doc;
    doc.format_version = cfio::SUPPORTED_FORMAT_VERSION;
    doc.assembly.name = "a";
    cfio::Component interposer;
    interposer.id = "interposer";
    interposer.type = "interposer";
    cfio::IOPad pad;
    pad.id = "P1";
    pad.io_class = "wire_bond";
    pad.layer = "TopMetal2";
    interposer.io_pads.push_back(pad);
    doc.components.push_back(interposer);
    cfio::Interface iface;
    iface.id = "link0";
    iface.type = "copper_pillar";
    cfio::InterfaceEndpoint to;
    to.component = "interposer";
    to.port_layer = "TopMetal2";
    iface.to = to;
    doc.interfaces.push_back(iface);
    check_throws([&] { cfio::dumps(doc); },
                 "the writer refuses to emit a rule 8 violation");
}

void test_interconnect_adapter_and_technology() {
    const std::string doc =
        "format_version: \"1.0\"\nassembly:\n  name: a\n"
        "interconnect:\n  adapter: vendorx\n"
        "  technology:\n    description: vendorx pillars\n    dbu: 0.001\n";
    cfio::ChipletDocument parsed = cfio::loads(doc);
    check(parsed.interconnect.has_value(), "interconnect parsed");
    check(parsed.interconnect->adapter == "vendorx", "adapter value");
    check(parsed.interconnect->technology.has_value(),
          "interconnect technology parsed");
    check(parsed.technology("vendorx") != nullptr,
          "interconnect technology is discoverable via lookup");
}

// technologies.<id>.stackup is the path to a stackup YAML a technology ships
// itself. This pins the emit as much as the parse: a consumer that re-saves a
// file whose technology declares one must not drop it, which is exactly what
// this reference did before the field was documented.
void test_technology_stackup_roundtrip() {
    const std::string doc =
        "format_version: \"1.0\"\nassembly:\n  name: a\n"
        "technologies:\n"
        "  intm4tm2:\n"
        "    description: interposer\n"
        "    layer_properties: ./tech/intm4tm2.lyp\n"
        "    stackup: ${INTERPOSER_PDK_ROOT}/libs.tech/chiplet_studio/intm4tm2.stackup.yaml\n"
        "    dbu: 0.001\n"
        "  sg13g2:\n"
        "    description: die\n"
        "    dbu: 0.001\n";
    cfio::ChipletDocument parsed = cfio::loads(doc);

    const cfio::Technology* interposer = parsed.technology("intm4tm2");
    check(interposer != nullptr, "technology intm4tm2 parsed");
    if (interposer) {
        // Verbatim, not resolved: the ${VAR} is the consumer's to expand.
        check(interposer->stackup ==
                  "${INTERPOSER_PDK_ROOT}/libs.tech/chiplet_studio/"
                  "intm4tm2.stackup.yaml",
              "stackup is kept verbatim");
    }

    // Absence stays absence, and must not become an emitted empty key.
    const cfio::Technology* die = parsed.technology("sg13g2");
    check(die != nullptr, "technology sg13g2 parsed");
    if (die) {
        check(die->stackup.empty(), "a technology without a stackup has none");
    }

    const std::string emitted = cfio::dumps(parsed);
    cfio::ChipletDocument reloaded = cfio::loads(emitted);
    const cfio::Technology* interposer2 = reloaded.technology("intm4tm2");
    check(interposer2 != nullptr && interposer2->stackup == interposer->stackup,
          "stackup survives dump/load");
    const cfio::Technology* die2 = reloaded.technology("sg13g2");
    check(die2 != nullptr && die2->stackup.empty(),
          "a technology without a stackup does not gain one on a round trip");
}

// True if any `#include` directive in `text` mentions `token`. Prose comments
// that merely name a library (e.g. documenting the dependency-clean intent) do
// not count -- only actual include directives.
bool has_include_of(const std::string& text, const std::string& token) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash == std::string::npos) continue;
        if (line.find("include", hash) == std::string::npos) continue;
        if (line.find(token, hash) != std::string::npos) return true;
    }
    return false;
}

void test_source_has_no_gpl_or_qt_dependency() {
    const std::string header = read_file(kHeaderFile);
    const std::string source = read_file(kSourceFile);

    // No GPL/Qt headers anywhere. (pcbnew is a Python module, included for
    // symmetry with the Python test's intent.)
    for (const std::string& banned : {std::string("klayout"), std::string("pcbnew"),
                                      std::string("Q")}) {
        check(!has_include_of(header, banned), "header includes no '" + banned + "' header");
        check(!has_include_of(source, banned), "source includes no '" + banned + "' header");
    }

    // The public header must stay dependency-clean: not even yaml-cpp leaks
    // through the API surface (it is confined to the .cpp).
    check(!has_include_of(header, "yaml"), "header does not include yaml-cpp");
}

const cfio::Component* find_component(const cfio::ChipletDocument& doc,
                                      const std::string& id) {
    for (const auto& c : doc.components) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

bool near(double a, double b) { return (a > b ? a - b : b - a) < 1e-6; }

// attachment_surface_z is the interposer's die-mount plane, decoupled from the
// physical body thickness. This pins the parse AND the emit: without the writer
// change a consumer that re-saves the file would silently drop the mount
// reference (the fixed-point test above cannot catch that -- the field is gone
// on the first load, so d1 already lacks it).
void test_attachment_surface_z_roundtrip() {
    const std::string path = kExamplesDir + "/interposer_demo_design.chiplet";
    cfio::ChipletDocument doc = cfio::load(path);

    const cfio::Component* interposer = find_component(doc, "interposer");
    check(interposer != nullptr, "example has an interposer component");
    if (interposer) {
        check(interposer->attachment_surface_z.has_value(),
              "interposer carries attachment_surface_z");
        check(near(interposer->attachment_surface_z.value_or(0.0), 13.83),
              "interposer attachment_surface_z is 13.83");
        // thickness is now the physical body, decoupled from the mount plane.
        check(near(interposer->dimensions.thickness, 300.0),
              "interposer thickness is the physical body, not the mount ref");
    }

    // A die carries no attachment surface -> nullopt (consumers fall back to
    // thickness). Absence is representable, not coerced to 0.
    const cfio::Component* die = find_component(doc, "U1");
    check(die != nullptr, "example has die U1");
    if (die) {
        check(!die->attachment_surface_z.has_value(),
              "a die has no attachment_surface_z");
    }

    // The value survives dump->load: the writer must emit it.
    cfio::ChipletDocument reloaded = cfio::loads(cfio::dumps(doc));
    const cfio::Component* interposer2 = find_component(reloaded, "interposer");
    check(interposer2 != nullptr &&
              interposer2->attachment_surface_z.has_value() &&
              near(interposer2->attachment_surface_z.value(), 13.83),
          "attachment_surface_z survives dump/load");
}

// --- H-B: tolerant format_version policy (parity with the Python matrix) ---

void test_higher_minor_warns_and_accepts() {
    const std::string text = "format_version: \"1.1\"\nassembly:\n  name: a\n";
    std::vector<std::string> sink;
    cfio::LoadOptions opts;
    opts.on_warn = [&sink](const std::string& m) { sink.push_back(m); };
    cfio::ChipletDocument doc = cfio::loads(text, opts);
    check(doc.format_version == "1.1",
          "same-major higher minor is accepted");
    check(!doc.warnings.empty(),
          "higher minor records a per-document warning");
    check(sink.size() == 1, "higher minor fires the on_warn sink exactly once");
}

void test_lower_and_higher_major_rejected() {
    check_throws([] {
        cfio::loads("format_version: \"2.0\"\nassembly:\n  name: a\n");
    }, "2.0 (higher major) rejected");
    check_throws([] {
        cfio::loads("format_version: \"0.9\"\nassembly:\n  name: a\n");
    }, "0.9 (lower major) rejected");
}

void test_malformed_version_rejected() {
    check_throws([] {
        cfio::loads("format_version: \"1\"\nassembly:\n  name: a\n");
    }, "\"1\" (no minor) rejected");
    check_throws([] {
        cfio::loads("format_version: \"1.0.0\"\nassembly:\n  name: a\n");
    }, "\"1.0.0\" (three parts) rejected");
}

void test_typed_writer_stamps_supported_for_higher_minor() {
    // The C++ struct writer is lossy (unknown top-level keys are not carried),
    // so a "1.1" input is written back as the supported "1.0" -- the OPPOSITE of
    // the Python passthrough writer, and the impl-class distinction the
    // conformance manifest encodes.
    const std::string text = "format_version: \"1.1\"\nassembly:\n  name: a\n";
    cfio::LoadOptions opts;
    opts.allow_intermediate = true;
    cfio::ChipletDocument doc = cfio::loads(text, opts);
    cfio::ChipletDocument reloaded = cfio::loads(cfio::dumps(doc));
    check(reloaded.format_version == cfio::SUPPORTED_FORMAT_VERSION,
          "typed/lossy writer stamps SUPPORTED for a higher-minor input");
}

// The reader RELEASE is declared and well formed. Its VALUE is compared with the
// Python reference in conformance/test_version_policy.py, the only place that
// sees both languages; here we pin that the constant exists, is a three-part
// MAJOR.MINOR.PATCH, and does not quietly disappear in a refactor.
void test_reader_release_is_declared() {
    const std::string release = cfio::READER_RELEASE;
    check(!release.empty(), "READER_RELEASE is not empty");
    std::istringstream parts(release);
    std::string part;
    int count = 0;
    bool numeric = true;
    while (std::getline(parts, part, '.')) {
        ++count;
        if (part.empty()) {
            numeric = false;
            continue;
        }
        for (char ch : part) {
            if (ch < '0' || ch > '9') {
                numeric = false;
            }
        }
    }
    check(count == 3, "READER_RELEASE is MAJOR.MINOR.PATCH");
    check(numeric, "READER_RELEASE components are numeric");
}

}  // namespace

// The version policy's transition window (SPEC-21), driven by
// conformance/fixtures/version_policy_cases.json: the same rows the Python
// reference runs in conformance/test_version_policy.py, so the two references
// are measured against one file instead of against each other.
//
// What this cannot run, and says so rather than dropping it: a row whose
// declared value is not a JSON string. check_contract_version takes a
// std::string, so a bare number, a null or a list has already been resolved by
// the loader before this reader is called; those rows carry declared_kind and
// are Python-only. Every other row runs here.
void test_version_policy_oracle() {
    const YAML::Node& oracle = version_oracle();
    int ran = 0;
    int skipped = 0;
    for (const auto& c : oracle["cases"]) {
        const std::string name = c["name"].as<std::string>();
        const std::string kind = c["declared_kind"].as<std::string>();
        if (kind != "string") { ++skipped; continue; }
        const std::string declared = c["declared"].as<std::string>();
        const std::string verdict = c["verdict"].as<std::string>();
        std::vector<std::string> accepted;
        for (const auto& a : c["accepted"]) {
            accepted.push_back(a.as<std::string>());
        }
        ++ran;
        if (verdict == "call_error") {
            // A programming error in the SET: std::invalid_argument, and
            // deliberately NOT ChipletFormatError, so a consumer's typo never
            // reads as a bad artifact. check_throws would accept the wrong one.
            ++g_checks;
            bool right = false;
            try {
                cfio::check_contract_version(declared, accepted, "io_pads.json");
            } catch (const cfio::ChipletFormatError&) {
                right = false;
            } catch (const std::invalid_argument&) {
                right = true;
            } catch (...) {
                right = false;
            }
            if (!right) {
                ++g_failures;
                std::cerr << "  FAIL (expected a call-time error): " << name
                          << "\n";
            }
            continue;
        }
        if (verdict == "refuse") {
            ++g_checks;
            std::string message;
            bool threw = false;
            try {
                cfio::check_contract_version(declared, accepted, "io_pads.json");
            } catch (const cfio::ChipletFormatError& e) {
                threw = true;
                message = e.what();
            } catch (...) {
            }
            bool named = threw;
            if (c["names_majors"]) {
                for (const auto& m : c["names_majors"]) {
                    if (message.find(m.as<std::string>()) == std::string::npos) {
                        named = false;
                    }
                }
                for (const std::string& spelling : accepted) {
                    if (message.find(spelling) == std::string::npos) {
                        named = false;
                    }
                }
            }
            if (!named) {
                ++g_failures;
                std::cerr << "  FAIL (refusal): " << name << ": " << message
                          << "\n";
            }
            continue;
        }
        // accept and accept_warn: same value back, and the warning fires for
        // exactly one of the two.
        std::vector<std::string> warned;
        std::string got;
        try {
            got = cfio::check_contract_version(
                declared, accepted, "io_pads.json",
                [&warned](const std::string& m) { warned.push_back(m); });
        } catch (const std::exception& e) {
            ++g_checks;
            ++g_failures;
            std::cerr << "  FAIL (unexpected throw): " << name << ": "
                      << e.what() << "\n";
            continue;
        }
        check(got == c["normalized"].as<std::string>(),
              name + ": normalized version");
        check(warned.size() == (verdict == "accept_warn" ? 1u : 0u),
              name + ": the higher-minor event is reported exactly when it is one");
    }
    check(ran >= 20, "the version oracle still carries its rows");
    check(skipped > 0,
          "the Python-only rows are still in the oracle, not quietly dropped");
}

// The .chiplet entry point reads the accepted SET, not a private copy of one
// supported major. Its Python twin swaps the tuple to prove the same thing; here
// the constant is a compile-time array, so what is checked is the shape of the
// refusal it produces from that array.
void test_format_version_refusal_names_every_accepted_major() {
    check(cfio::ACCEPTED_FORMAT_VERSIONS.size() >= 1,
          "the reader accepts at least one major");
    std::string message;
    try {
        cfio::check_format_version("2.0");
    } catch (const cfio::ChipletFormatError& e) {
        message = e.what();
    }
    for (const char* entry : cfio::ACCEPTED_FORMAT_VERSIONS) {
        check(message.find(entry) != std::string::npos,
              std::string("the refusal names the accepted version ") + entry);
    }
    const bool one = cfio::ACCEPTED_FORMAT_VERSIONS.size() == 1;
    check(message.find(one ? "supports major " : "supports majors ")
              != std::string::npos,
          "the refusal names the accepted majors in the shared phrasing");
}

int main() {
    std::cout << "chiplet_format_io C++ reference tests\n";

    // The grammar tests are driven by the shared oracle. If it is not where we
    // looked, say so and stop: several tests below would otherwise be skipped by
    // an exception escaping main, and a run that never opened the oracle must
    // never look like a run that agreed with it.
    const std::string oracle = oracle_path();
    if (!std::filesystem::exists(oracle)) {
        std::cerr << "  FAIL: top-level block grammar oracle not found at "
                  << oracle << "\n"
                  << "        (set CHIPLET_BLOCK_ORACLE to the path of "
                     "conformance/fixtures/top_level_blocks_cases.json)\n";
        std::cout << "1 checks, 1 failures\nFAILED\n";
        return 1;
    }

    const std::string version_cases = version_oracle_path();
    if (!std::filesystem::exists(version_cases)) {
        std::cerr << "  FAIL: version policy oracle not found at "
                  << version_cases << "\n"
                  << "        (set CHIPLET_VERSION_ORACLE to the path of "
                     "conformance/fixtures/version_policy_cases.json)\n";
        std::cout << "1 checks, 1 failures\nFAILED\n";
        return 1;
    }

    test_roundtrip_canonical_example();
    test_all_example_chiplets_parse();
    test_missing_format_version_rejected();
    test_unsupported_version_rejected();
    test_assembly_name_required();
    test_component_requires_id_and_type();
    test_intermediate_refused_then_allowed();
    test_dump_roundtrips_components_order();
    test_attachment_surface_z_roundtrip();
    test_unknown_interface_type_rejected();
    test_every_known_interface_type_is_accepted();
    test_pad_usage_rule_refuses_a_mismatched_pad();
    test_flow_block_is_the_exact_source_slice();
    test_flow_block_is_re_emitted_byte_for_byte();
    test_quoted_key_at_column_zero_loads_and_may_refuse_to_write();
    test_flow_block_the_grammar_cannot_delimit_loads_but_does_not_write();
    test_hand_built_flow_value_without_a_key_line_still_emits();
    test_interconnect_adapter_and_technology();
    test_technology_stackup_roundtrip();
    test_higher_minor_warns_and_accepts();
    test_lower_and_higher_major_rejected();
    test_malformed_version_rejected();
    test_typed_writer_stamps_supported_for_higher_minor();
    test_reader_release_is_declared();
    test_version_policy_oracle();
    test_format_version_refusal_names_every_accepted_major();
    test_source_has_no_gpl_or_qt_dependency();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    if (g_failures == 0) {
        std::cout << "OK\n";
        return 0;
    }
    std::cout << "FAILED\n";
    return 1;
}
