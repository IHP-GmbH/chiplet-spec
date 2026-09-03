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
const std::string kBlockOracle = CHIPLET_BLOCK_ORACLE;

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
    const YAML::Node oracle = YAML::LoadFile(kBlockOracle);
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
        check(parsed.flow_yaml == expected,
              name + ": flow_yaml is the source slice, byte for byte");
    }
    check(cases >= 5, "the oracle still carries the flow split cases");
}

// Rule 4 end to end: a document this writer did not author goes out with the
// same bytes it came in with. A node dump fails this on the comment alone.
void test_flow_block_is_re_emitted_byte_for_byte() {
    const YAML::Node oracle = YAML::LoadFile(kBlockOracle);
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
// it away on the next export. This reader splits, so it refuses the document.
void test_quoted_key_at_column_zero_refused() {
    const YAML::Node oracle = YAML::LoadFile(kBlockOracle);
    int cases = 0;
    for (const auto& c : oracle["refuse"]) {
        ++cases;
        const std::string doc = c["doc"].as<std::string>();
        check_throws([&] { cfio::loads(doc); },
                     c["name"].as<std::string>() + ": refused");
    }
    check(cases >= 3, "the oracle still carries the refuse cases");
}

// A flow block the grammar cannot delimit: a flow-style document, or a key line
// spelled `flow :`, which YAML reads as the key `flow` and the grammar does not
// see at all. Either way the block has no slice, so re-emitting it byte for byte
// is impossible; the reader says so instead of handing back an empty field that
// dumps() would drop, or a dump that looks like the source text and is not.
void test_flow_block_the_grammar_cannot_delimit_is_refused() {
    check_throws([&] {
        cfio::loads("{format_version: \"1.0\", assembly: {name: a}, "
                    "flow: {steps: []}}\n");
    }, "flow-style document refused");
    check_throws([&] {
        cfio::loads("format_version: \"1.0\"\nassembly:\n  name: a\n"
                    "flow :\n  steps: []\n");
    }, "`flow :` refused");
    // Without a flow block, both spellings of the same document are fine: the
    // grammar only has to delimit what is there.
    cfio::ChipletDocument parsed =
        cfio::loads("{format_version: \"1.0\", assembly: {name: a}}\n");
    check(!parsed.has_flow && parsed.assembly.name == "a",
          "a flow-style document without a flow block still loads");
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

int main() {
    std::cout << "chiplet_format_io C++ reference tests\n";
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
    test_flow_block_is_the_exact_source_slice();
    test_flow_block_is_re_emitted_byte_for_byte();
    test_quoted_key_at_column_zero_refused();
    test_flow_block_the_grammar_cannot_delimit_is_refused();
    test_hand_built_flow_value_without_a_key_line_still_emits();
    test_interconnect_adapter_and_technology();
    test_technology_stackup_roundtrip();
    test_higher_minor_warns_and_accepts();
    test_lower_and_higher_major_rejected();
    test_malformed_version_rejected();
    test_typed_writer_stamps_supported_for_higher_minor();
    test_reader_release_is_declared();
    test_source_has_no_gpl_or_qt_dependency();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    if (g_failures == 0) {
        std::cout << "OK\n";
        return 0;
    }
    std::cout << "FAILED\n";
    return 1;
}
