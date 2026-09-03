// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 IHP GmbH
//
// chiplet_format_io -- permissive reference reader/writer for the .chiplet format.
//
// Apache-2.0. Dependency-clean: the only third-party dependency is yaml-cpp
// (MIT). This header exposes ONLY the standard library; yaml-cpp is confined to
// the implementation. The library deliberately does NOT include any KLayout, Qt
// or pcbnew header, so it can be embedded in tools under any license,
// open-source or proprietary.
//
// This is an INDEPENDENT reference implementation of the format described in
// docs/CHIPLET_FORMAT_SPEC.md and mirrors the Python reference library
// (reference/python/chiplet_format_io). It is intentionally *not* the byte-exact
// writer used inside the GPL host tools (the KiCad plugin / KiCad fork
// exporter): those are locked to each other by a byte-exact parity gate. Output
// here is canonical YAML, semantically equivalent, not byte-identical to those
// hosts.
//
// Typical use:
//
//     #include <chiplet_format_io/chiplet_format_io.hpp>
//     namespace cfio = chiplet_format_io;
//     cfio::ChipletDocument doc = cfio::load("design.chiplet");
//     doc.assembly.name = "renamed";
//     cfio::dump(doc, "design.chiplet");

#ifndef CHIPLET_FORMAT_IO_HPP
#define CHIPLET_FORMAT_IO_HPP

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chiplet_format_io {

// The highest format_version this reference implementation was written for. The
// on-disk baseline stays additive-stable at "1.0"; readers are tolerant of a
// same-major higher minor (see check_format_version). Bump together with
// docs/CHIPLET_FORMAT_SPEC.md and the Python reference library.
inline constexpr const char* SUPPORTED_FORMAT_VERSION = "1.0";

// The release of THIS reference implementation, mirroring the Python library's
// chiplet_format_io.__version__. Distinct from SUPPORTED_FORMAT_VERSION, which
// is a fact about documents: a consumer that vendors a copy of this reader pins
// a reader RELEASE, so a vendored mirror can be gated on a version instead of on
// bytes. The two reference implementations ship one release number, and
// conformance/test_version_policy.py fails if they drift apart.
inline constexpr const char* READER_RELEASE = "1.1.0";

// Apply the tolerant format_version policy (parity-bound to the Python
// check_format_version): missing/malformed or a different major throws
// ChipletFormatError; a same-major minor <= supported is accepted silently; a
// same-major higher minor is accepted and, when on_warn is set, reported through
// it (never stderr, never a throw). Returns the normalized "MAJOR.MINOR".
std::string check_format_version(
    const std::string& fv,
    const std::function<void(const std::string&)>& on_warn = {});

// Raised when a .chiplet document is malformed or unsupported. Named to mirror
// the Python ChipletFormatError. Host tools that wrap this library translate it
// into their own exception type to preserve their public contract.
class ChipletFormatError : public std::runtime_error {
public:
    explicit ChipletFormatError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// --- Plain value structs (a faithful, lossless view of the .chiplet schema) ---
//
// Enum-like fields (component type, interface type, io_class, orientation,
// anchor, net class) are kept as their canonical YAML *strings*. The format
// owns the vocabulary; consumers (e.g. a GPL host) map these strings into their
// own richer enums and own any UX such as warnings on unknown values. Paths are
// kept verbatim as written in the file -- this library never touches the
// filesystem for resolution; that is the consumer's responsibility.

struct Position3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Rotation3D {
    double z = 0.0;
};

struct Dimensions3D {
    double width = 0.0;
    double height = 0.0;
    double thickness = 0.0;
};

struct AssemblyMeta {
    std::string name;            // required
    std::string description;
    std::string author;
    std::string created;
    std::string modified;
    std::string units;
    std::string assembly_gds;    // verbatim path
    std::string io_technology;
};

struct Technology {
    std::string id;              // map key in the YAML
    std::string description;
    std::string layer_properties;  // verbatim path to the .lyp
    // Optional path to a layer-stackup YAML this technology ships. Verbatim as
    // read: the consumer resolves it through the same ${VAR}/relative chain as
    // layer_properties, and an explicit value takes priority over whatever
    // stackup the consumer would otherwise look up for this id. Empty means the
    // field was absent.
    std::string stackup;
    double dbu = 0.001;
    bool has_dbu = false;        // whether dbu was present in the file
};

struct ConnectionStackLayer {
    std::string name;
    std::string material;
    double height = 0.0;
    double diameter = 0.0;
};

struct ConnectionStack {
    std::string id;              // map key in the YAML
    std::string description;
    std::vector<ConnectionStackLayer> layers;
};

struct ComponentArray {
    std::string pattern;
    int count_x = 0;
    int count_y = 0;
    double pitch_x = 0.0;
    double pitch_y = 0.0;
    Position3D start_position;
};

struct IOPad {
    std::string id;
    std::string io_class;        // canonical string, e.g. "wire_bond"
    std::string net;
    double pos_x = 0.0;
    double pos_y = 0.0;
    double size_x = 0.0;
    double size_y = 0.0;
    std::string layer;
};

struct Component {
    std::string id;              // required
    std::string type;            // required, canonical string ("die", ...)
    std::string technology;
    std::string connection;
    std::string layout;          // verbatim path
    // Both the legacy `top_cell` scalar and the `cells` sequence collapse here.
    // On write, a single entry is emitted as `top_cell` (back-compat), multiple
    // as `cells` -- mirroring the host writer.
    std::vector<std::string> cells;
    Position3D position;
    Rotation3D rotation;
    // Raw orientation string. The contract defines only "face_up" (default)
    // and "flip_chip" (see coord_frame_contract.md 2.4); "face_down" is a
    // non-canonical alias consumers may accept-with-warning. Empty means the
    // field was absent (treated as face_up downstream). The consumer validates
    // the value.
    std::string orientation;
    // Raw anchor string if the field was present; std::nullopt if absent. The
    // consumer validates the value and owns the "missing anchor" warning.
    std::optional<std::string> anchor;
    Dimensions3D dimensions;
    // Interposer die-attachment (BEOL-top) surface z, in the component's local
    // frame: the plane dies mount on (a die's position.z == this +
    // connection-stack height). std::nullopt when the field is absent, in
    // which case consumers fall back to dimensions.thickness as the mount
    // reference -- legacy files where thickness encoded the attachment
    // surface. When present, dimensions.thickness is the physical body
    // z-extent (the interposer substrate, extending downward from this
    // surface), decoupled from the mount plane. See coord_frame_contract.md
    // sections 3.2 / 3.4 / 5.5.
    std::optional<double> attachment_surface_z;
    std::optional<ComponentArray> array;
    // Insertion-ordered key/value metadata.
    std::vector<std::pair<std::string, std::string>> metadata;
    std::vector<IOPad> io_pads;
};

struct InterfaceEndpoint {
    std::string component;
    std::string surface;
    std::string port_layer;
};

struct InterfacePhysical {
    double pitch = 0.0;
    double diameter = 0.0;
    double height = 0.0;
};

struct Interface {
    std::string id;              // required
    std::string type;            // required, validated against the known set
    std::optional<InterfaceEndpoint> from;
    std::optional<InterfaceEndpoint> to;
    std::optional<InterfacePhysical> physical;
};

struct NetConnection {
    std::string component;
    std::string pin;
    std::string layer;
};

struct Net {
    std::string name;            // required
    std::string net_class = "signal";  // canonical string; default "signal"
    bool external = false;
    std::vector<NetConnection> connections;
};

struct Netlist {
    bool present = false;
    std::vector<Net> nets;
    std::string external_netlist;  // verbatim path
};

struct Interconnect {
    std::string adapter;                  // required when the block is present
    std::optional<Technology> technology; // optional PDK-backed identity
};

struct Metadata {
    bool present = false;
    bool finalize_required = false;
    std::string finalizer;
};

// Top-level parsed document. A faithful, round-trippable view of one .chiplet
// file. The `flow` build block is host-specific build configuration the spec
// leaves opaque, and it is kept as the exact source text rather than as a parsed
// node; consumers that care re-parse `flow_yaml` themselves.
struct ChipletDocument {
    std::string format_version;            // always "1.0" after a successful load
    Metadata metadata;                     // the _metadata block
    AssemblyMeta assembly;
    std::vector<Technology> technologies;          // insertion order preserved
    std::vector<ConnectionStack> connection_stacks; // insertion order preserved
    std::vector<Component> components;
    std::optional<Interconnect> interconnect;
    std::vector<Interface> interfaces;
    Netlist netlist;
    bool has_flow = false;
    // The `flow` block exactly as it stands in the source: the `flow:` key line
    // included, original line endings, no trailing-newline normalisation,
    // nothing stripped. Delimited by the top-level block grammar
    // (docs/CHIPLET_FORMAT_SPEC.md), which is what lets a host that did not
    // author the block re-emit it byte for byte, as flow rule 4 requires. It is
    // NOT a re-serialisation: a node dump re-quotes scalars (a source '0755'
    // comes back bare, and is then an integer to the next PyYAML reader) and
    // drops comments. dumps() writes these bytes back unchanged.
    std::string flow_yaml;

    // Non-fatal reader notes (e.g. a same-major higher-minor format_version).
    // Per-document, so there is no global state and a GUI host can surface them
    // however it likes; never written to stderr by the library.
    std::vector<std::string> warnings;

    // Convenience lookup; returns nullptr if no technology with that id exists.
    const Technology* technology(const std::string& id) const;
};

struct LoadOptions {
    bool allow_intermediate = false;  // accept _metadata.finalize_required files
    bool validate = true;             // run semantic validation after parsing
    // Optional sink for non-fatal reader notes. When unset, notes still land in
    // ChipletDocument::warnings. Never called with a fatal condition (those throw).
    std::function<void(const std::string&)> on_warn;
};

struct DumpOptions {
    bool validate = true;             // validate before serializing
};

// Parse a .chiplet document from a YAML string. Throws ChipletFormatError on a
// malformed or unsupported document.
ChipletDocument loads(const std::string& text, const LoadOptions& opts = {});

// Read and parse a .chiplet file. Throws ChipletFormatError on I/O or parse
// failure.
ChipletDocument load(const std::string& path, const LoadOptions& opts = {});

// Serialize a document to a canonical YAML string (semantic, not byte-exact to
// the GPL host writers).
std::string dumps(const ChipletDocument& doc, const DumpOptions& opts = {});

// Serialize a document to a file.
void dump(const ChipletDocument& doc, const std::string& path,
          const DumpOptions& opts = {});

// Validate the semantic invariants that survive into the struct model
// (format_version, intermediate guard, assembly.name, component id/type,
// interface id/type, netlist net.name). Structural checks (a section being a
// map vs a sequence) happen during parsing. Throws ChipletFormatError on the
// first violation. Set allow_intermediate to accept finalize_required documents.
void validate(const ChipletDocument& doc, bool allow_intermediate = false,
              const std::function<void(const std::string&)>& on_warn = {});

}  // namespace chiplet_format_io

#endif  // CHIPLET_FORMAT_IO_HPP
