// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 IHP GmbH
//
// chiplet_format_io implementation. yaml-cpp (MIT) is used here and ONLY here;
// the public header stays dependency-clean.

#include "chiplet_format_io/chiplet_format_io.hpp"

#include <yaml-cpp/yaml.h>

#include <array>
#include <fstream>
#include <sstream>

namespace chiplet_format_io {

namespace {

// Read an optional scalar, returning `fallback` when the key is absent or null.
template <typename T>
T as_or(const YAML::Node& node, const char* key, const T& fallback) {
    const YAML::Node child = node[key];
    if (!child || child.IsNull()) {
        return fallback;
    }
    return child.as<T>(fallback);
}

Position3D parse_position(const YAML::Node& node) {
    Position3D p;
    p.x = as_or<double>(node, "x", 0.0);
    p.y = as_or<double>(node, "y", 0.0);
    p.z = as_or<double>(node, "z", 0.0);
    return p;
}

Rotation3D parse_rotation(const YAML::Node& node) {
    Rotation3D r;
    r.z = as_or<double>(node, "z", 0.0);
    return r;
}

Dimensions3D parse_dimensions(const YAML::Node& node) {
    Dimensions3D d;
    d.width = as_or<double>(node, "width", 0.0);
    d.height = as_or<double>(node, "height", 0.0);
    d.thickness = as_or<double>(node, "thickness", 0.0);
    return d;
}

Technology parse_technology(const std::string& id, const YAML::Node& node) {
    Technology tech;
    tech.id = id;
    tech.description = as_or<std::string>(node, "description", "");
    tech.layer_properties = as_or<std::string>(node, "layer_properties", "");
    if (node["dbu"] && !node["dbu"].IsNull()) {
        tech.dbu = node["dbu"].as<double>(0.001);
        tech.has_dbu = true;
    }
    return tech;
}

Component parse_component(const YAML::Node& node) {
    if (!node.IsMap()) {
        throw ChipletFormatError("component entry must be a mapping");
    }
    Component c;

    // id and type are hard-required by the format (a component without them is
    // meaningless); throw unconditionally, mirroring the C++ host reader.
    if (!node["id"] || node["id"].IsNull()) {
        throw ChipletFormatError("component missing required field: id");
    }
    c.id = node["id"].as<std::string>();

    if (!node["type"] || node["type"].IsNull()) {
        throw ChipletFormatError("component '" + c.id +
                                 "' missing required field: type");
    }
    c.type = node["type"].as<std::string>();

    c.technology = as_or<std::string>(node, "technology", "");
    c.connection = as_or<std::string>(node, "connection", "");
    c.layout = as_or<std::string>(node, "layout", "");

    // cells: new `cells` (sequence or scalar) or legacy `top_cell` scalar.
    if (node["cells"]) {
        const YAML::Node& cells = node["cells"];
        if (cells.IsSequence()) {
            for (const auto& cell : cells) {
                c.cells.push_back(cell.as<std::string>());
            }
        } else if (!cells.IsNull()) {
            c.cells.push_back(cells.as<std::string>());
        }
    } else if (node["top_cell"] && !node["top_cell"].IsNull()) {
        c.cells.push_back(node["top_cell"].as<std::string>());
    }

    if (node["position"]) c.position = parse_position(node["position"]);
    if (node["rotation"]) c.rotation = parse_rotation(node["rotation"]);
    c.orientation = as_or<std::string>(node, "orientation", "");

    if (node["anchor"] && !node["anchor"].IsNull()) {
        c.anchor = node["anchor"].as<std::string>();
    }

    if (node["dimensions"]) c.dimensions = parse_dimensions(node["dimensions"]);

    // Optional interposer die-attachment surface z. Absent => nullopt, and
    // consumers fall back to dimensions.thickness (legacy mount reference).
    if (node["attachment_surface_z"] && !node["attachment_surface_z"].IsNull()) {
        c.attachment_surface_z = node["attachment_surface_z"].as<double>();
    }

    if (node["array"]) {
        const YAML::Node& a = node["array"];
        ComponentArray arr;
        arr.pattern = as_or<std::string>(a, "pattern", "");
        if (a["count"]) {
            arr.count_x = as_or<int>(a["count"], "x", 0);
            arr.count_y = as_or<int>(a["count"], "y", 0);
        }
        if (a["pitch"]) {
            arr.pitch_x = as_or<double>(a["pitch"], "x", 0.0);
            arr.pitch_y = as_or<double>(a["pitch"], "y", 0.0);
        }
        if (a["start_position"]) {
            arr.start_position = parse_position(a["start_position"]);
        }
        c.array = arr;
    }

    if (node["metadata"] && node["metadata"].IsMap()) {
        for (const auto& kv : node["metadata"]) {
            c.metadata.emplace_back(kv.first.as<std::string>(),
                                    kv.second.as<std::string>());
        }
    }

    if (node["io_pads"] && node["io_pads"].IsSequence()) {
        for (const auto& padNode : node["io_pads"]) {
            IOPad pad;
            pad.id = as_or<std::string>(padNode, "id", "");
            pad.io_class = as_or<std::string>(padNode, "io_class", "");
            pad.net = as_or<std::string>(padNode, "net", "");
            if (padNode["position"]) {
                pad.pos_x = as_or<double>(padNode["position"], "x", 0.0);
                pad.pos_y = as_or<double>(padNode["position"], "y", 0.0);
            }
            if (padNode["size"]) {
                pad.size_x = as_or<double>(padNode["size"], "x", 0.0);
                pad.size_y = as_or<double>(padNode["size"], "y", 0.0);
            }
            pad.layer = as_or<std::string>(padNode, "layer", "");
            c.io_pads.push_back(std::move(pad));
        }
    }

    return c;
}

ConnectionStack parse_connection_stack(const std::string& id,
                                       const YAML::Node& node) {
    ConnectionStack stack;
    stack.id = id;
    stack.description = as_or<std::string>(node, "description", "");
    if (node["layers"] && node["layers"].IsSequence()) {
        for (const auto& layerNode : node["layers"]) {
            ConnectionStackLayer layer;
            layer.name = as_or<std::string>(layerNode, "name", "");
            layer.material = as_or<std::string>(layerNode, "material", "");
            layer.height = as_or<double>(layerNode, "height", 0.0);
            layer.diameter = as_or<double>(layerNode, "diameter", 0.0);
            stack.layers.push_back(std::move(layer));
        }
    }
    return stack;
}

const std::array<const char*, 4> kKnownInterfaceTypes = {
    "micro_bump", "copper_pillar", "tsv", "wire_bond"};

bool is_known_interface_type(const std::string& t) {
    for (const char* k : kKnownInterfaceTypes) {
        if (t == k) return true;
    }
    return false;
}

InterfaceEndpoint parse_endpoint(const YAML::Node& node) {
    InterfaceEndpoint ep;
    ep.component = as_or<std::string>(node, "component", "");
    ep.surface = as_or<std::string>(node, "surface", "");
    ep.port_layer = as_or<std::string>(node, "port_layer", "");
    return ep;
}

Interface parse_interface(const YAML::Node& node) {
    if (!node["id"] || node["id"].IsNull()) {
        throw ChipletFormatError("interface missing required field: id");
    }
    if (!node["type"] || node["type"].IsNull()) {
        throw ChipletFormatError("interface missing required field: type");
    }
    Interface iface;
    iface.id = node["id"].as<std::string>();
    iface.type = node["type"].as<std::string>();
    if (!is_known_interface_type(iface.type)) {
        throw ChipletFormatError("unknown interface type: " + iface.type);
    }
    if (node["from"]) iface.from = parse_endpoint(node["from"]);
    if (node["to"]) iface.to = parse_endpoint(node["to"]);
    if (node["physical"]) {
        InterfacePhysical phys;
        phys.pitch = as_or<double>(node["physical"], "pitch", 0.0);
        phys.diameter = as_or<double>(node["physical"], "diameter", 0.0);
        phys.height = as_or<double>(node["physical"], "height", 0.0);
        iface.physical = phys;
    }
    return iface;
}

Netlist parse_netlist(const YAML::Node& node) {
    Netlist nl;
    nl.present = true;
    if (node["nets"] && node["nets"].IsSequence()) {
        for (const auto& netNode : node["nets"]) {
            if (!netNode["name"] || netNode["name"].IsNull()) {
                throw ChipletFormatError("netlist net missing required field: name");
            }
            Net net;
            net.name = netNode["name"].as<std::string>();
            net.net_class = as_or<std::string>(netNode, "class", "signal");
            net.external = as_or<bool>(netNode, "external", false);
            if (netNode["connections"] && netNode["connections"].IsSequence()) {
                for (const auto& connNode : netNode["connections"]) {
                    NetConnection conn;
                    conn.component = as_or<std::string>(connNode, "component", "");
                    conn.pin = as_or<std::string>(connNode, "pin", "");
                    conn.layer = as_or<std::string>(connNode, "layer", "");
                    net.connections.push_back(std::move(conn));
                }
            }
            nl.nets.push_back(std::move(net));
        }
    }
    nl.external_netlist = as_or<std::string>(node, "external_netlist", "");
    return nl;
}

// Up-front document gate: fail fast on the document-level invariants exactly
// where the C++ host reader does, before any section is consumed.
void check_document_gate(const YAML::Node& root, const std::string& formatVersion,
                         const Metadata& metadata, bool allow_intermediate) {
    if (!root["format_version"]) {
        throw ChipletFormatError("missing required key: format_version");
    }
    if (formatVersion != SUPPORTED_FORMAT_VERSION) {
        throw ChipletFormatError("unsupported format_version '" + formatVersion +
                                 "'; this reader supports \"" +
                                 std::string(SUPPORTED_FORMAT_VERSION) + "\"");
    }
    if (metadata.finalize_required && !allow_intermediate) {
        const std::string finalizer = metadata.finalizer.empty()
            ? "hyp_to_gds.py --update-chiplet-file" : metadata.finalizer;
        throw ChipletFormatError(
            "this is an intermediate .chiplet (_metadata.finalize_required: "
            "true); run " + finalizer + " to finalize, or pass "
            "allow_intermediate=true");
    }
    if (!root["assembly"] || !root["assembly"].IsMap()) {
        throw ChipletFormatError("missing or invalid 'assembly' section");
    }
    const YAML::Node name = root["assembly"]["name"];
    if (!name || name.IsNull() || name.as<std::string>("").empty()) {
        throw ChipletFormatError("assembly.name is required");
    }
}

void emit_technology_fields(YAML::Emitter& out, const Technology& tech) {
    if (!tech.description.empty()) {
        out << YAML::Key << "description" << YAML::Value << tech.description;
    }
    if (!tech.layer_properties.empty()) {
        out << YAML::Key << "layer_properties" << YAML::Value
            << tech.layer_properties;
    }
    if (tech.has_dbu) {
        out << YAML::Key << "dbu" << YAML::Value << tech.dbu;
    }
}

}  // namespace

const Technology* ChipletDocument::technology(const std::string& id) const {
    for (const auto& t : technologies) {
        if (t.id == id) return &t;
    }
    if (interconnect && interconnect->technology &&
        interconnect->technology->id == id) {
        return &interconnect->technology.value();
    }
    return nullptr;
}

void validate(const ChipletDocument& doc, bool allow_intermediate) {
    if (doc.format_version != SUPPORTED_FORMAT_VERSION) {
        throw ChipletFormatError("unsupported format_version '" +
                                 doc.format_version + "'; expected \"" +
                                 std::string(SUPPORTED_FORMAT_VERSION) + "\"");
    }
    if (doc.metadata.finalize_required && !allow_intermediate) {
        const std::string finalizer = doc.metadata.finalizer.empty()
            ? "hyp_to_gds.py --update-chiplet-file" : doc.metadata.finalizer;
        throw ChipletFormatError(
            "this is an intermediate .chiplet (_metadata.finalize_required: "
            "true); run " + finalizer + " to finalize, or pass "
            "allow_intermediate=true");
    }
    if (doc.assembly.name.empty()) {
        throw ChipletFormatError("assembly.name is required");
    }
    for (const auto& c : doc.components) {
        if (c.id.empty()) {
            throw ChipletFormatError("component missing required field: id");
        }
        if (c.type.empty()) {
            throw ChipletFormatError("component '" + c.id +
                                     "' missing required field: type");
        }
    }
    for (const auto& iface : doc.interfaces) {
        if (iface.id.empty()) {
            throw ChipletFormatError("interface missing required field: id");
        }
        if (iface.type.empty() || !is_known_interface_type(iface.type)) {
            throw ChipletFormatError("interface '" + iface.id +
                                     "' has missing or unknown type");
        }
    }
    for (const auto& net : doc.netlist.nets) {
        if (net.name.empty()) {
            throw ChipletFormatError("netlist net missing required field: name");
        }
    }
}

ChipletDocument loads(const std::string& text, const LoadOptions& opts) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& e) {
        throw ChipletFormatError(std::string("YAML parse error: ") + e.what());
    }

    if (!root || root.IsNull()) {
        throw ChipletFormatError("empty .chiplet document");
    }
    if (!root.IsMap()) {
        throw ChipletFormatError("top-level .chiplet document must be a mapping");
    }

    ChipletDocument doc;
    doc.format_version = as_or<std::string>(root, "format_version", "");

    if (root["_metadata"] && root["_metadata"].IsMap()) {
        doc.metadata.present = true;
        doc.metadata.finalize_required =
            as_or<bool>(root["_metadata"], "finalize_required", false);
        doc.metadata.finalizer =
            as_or<std::string>(root["_metadata"], "finalizer", "");
    }

    if (opts.validate) {
        check_document_gate(root, doc.format_version, doc.metadata,
                            opts.allow_intermediate);
    }

    if (root["assembly"]) {
        const YAML::Node& a = root["assembly"];
        doc.assembly.name = as_or<std::string>(a, "name", "");
        doc.assembly.description = as_or<std::string>(a, "description", "");
        doc.assembly.author = as_or<std::string>(a, "author", "");
        doc.assembly.created = as_or<std::string>(a, "created", "");
        doc.assembly.modified = as_or<std::string>(a, "modified", "");
        doc.assembly.units = as_or<std::string>(a, "units", "");
        doc.assembly.assembly_gds = as_or<std::string>(a, "assembly_gds", "");
        doc.assembly.io_technology = as_or<std::string>(a, "io_technology", "");
    }

    if (root["technologies"]) {
        if (!root["technologies"].IsMap()) {
            throw ChipletFormatError("'technologies' must be a mapping");
        }
        for (const auto& item : root["technologies"]) {
            doc.technologies.push_back(
                parse_technology(item.first.as<std::string>(), item.second));
        }
    }

    if (root["connection_stacks"]) {
        if (!root["connection_stacks"].IsMap()) {
            throw ChipletFormatError("'connection_stacks' must be a mapping");
        }
        for (const auto& item : root["connection_stacks"]) {
            doc.connection_stacks.push_back(
                parse_connection_stack(item.first.as<std::string>(), item.second));
        }
    }

    if (root["components"]) {
        if (!root["components"].IsSequence()) {
            throw ChipletFormatError("'components' must be a sequence");
        }
        for (const auto& compNode : root["components"]) {
            doc.components.push_back(parse_component(compNode));
        }
    }

    // Interconnect block is recognized only when it carries an adapter,
    // mirroring the host reader.
    if (root["interconnect"] && root["interconnect"]["adapter"]) {
        Interconnect ic;
        ic.adapter = root["interconnect"]["adapter"].as<std::string>();
        if (root["interconnect"]["technology"]) {
            ic.technology =
                parse_technology(ic.adapter, root["interconnect"]["technology"]);
        }
        doc.interconnect = std::move(ic);
    }

    if (root["interfaces"]) {
        if (!root["interfaces"].IsSequence()) {
            throw ChipletFormatError("'interfaces' must be a sequence");
        }
        for (const auto& ifaceNode : root["interfaces"]) {
            doc.interfaces.push_back(parse_interface(ifaceNode));
        }
    }

    if (root["netlist"]) {
        doc.netlist = parse_netlist(root["netlist"]);
    }

    if (root["flow"]) {
        doc.has_flow = true;
        doc.flow_yaml = YAML::Dump(root["flow"]);
    }

    return doc;
}

ChipletDocument load(const std::string& path, const LoadOptions& opts) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw ChipletFormatError("could not open file for reading: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return loads(buffer.str(), opts);
}

std::string dumps(const ChipletDocument& doc, const DumpOptions& opts) {
    if (opts.validate) {
        // Writing an intermediate (finalize_required) document is legitimate.
        validate(doc, /*allow_intermediate=*/true);
    }

    YAML::Emitter out;
    out << YAML::BeginMap;

    const std::string fv =
        doc.format_version.empty() ? SUPPORTED_FORMAT_VERSION : doc.format_version;
    out << YAML::Key << "format_version" << YAML::Value << YAML::DoubleQuoted << fv;

    if (doc.metadata.finalize_required || !doc.metadata.finalizer.empty()) {
        out << YAML::Key << "_metadata" << YAML::Value << YAML::BeginMap;
        if (doc.metadata.finalize_required) {
            out << YAML::Key << "finalize_required" << YAML::Value << true;
        }
        if (!doc.metadata.finalizer.empty()) {
            out << YAML::Key << "finalizer" << YAML::Value << doc.metadata.finalizer;
        }
        out << YAML::EndMap;
    }

    // Assembly metadata.
    out << YAML::Key << "assembly" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << doc.assembly.name;
    if (!doc.assembly.description.empty())
        out << YAML::Key << "description" << YAML::Value << doc.assembly.description;
    if (!doc.assembly.author.empty())
        out << YAML::Key << "author" << YAML::Value << doc.assembly.author;
    if (!doc.assembly.created.empty())
        out << YAML::Key << "created" << YAML::Value << doc.assembly.created;
    if (!doc.assembly.modified.empty())
        out << YAML::Key << "modified" << YAML::Value << doc.assembly.modified;
    if (!doc.assembly.units.empty())
        out << YAML::Key << "units" << YAML::Value << doc.assembly.units;
    if (!doc.assembly.assembly_gds.empty())
        out << YAML::Key << "assembly_gds" << YAML::Value << doc.assembly.assembly_gds;
    if (!doc.assembly.io_technology.empty())
        out << YAML::Key << "io_technology" << YAML::Value << doc.assembly.io_technology;
    out << YAML::EndMap;

    // Technologies.
    if (!doc.technologies.empty()) {
        out << YAML::Key << "technologies" << YAML::Value << YAML::BeginMap;
        for (const auto& tech : doc.technologies) {
            out << YAML::Key << tech.id << YAML::Value << YAML::BeginMap;
            emit_technology_fields(out, tech);
            out << YAML::EndMap;
        }
        out << YAML::EndMap;
    }

    // Interconnect (adapter + optional PDK-backed identity).
    if (doc.interconnect) {
        out << YAML::Key << "interconnect" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "adapter" << YAML::Value << doc.interconnect->adapter;
        if (doc.interconnect->technology) {
            out << YAML::Key << "technology" << YAML::Value << YAML::BeginMap;
            emit_technology_fields(out, doc.interconnect->technology.value());
            out << YAML::EndMap;
        }
        out << YAML::EndMap;
    }

    // Connection stacks.
    if (!doc.connection_stacks.empty()) {
        out << YAML::Key << "connection_stacks" << YAML::Value << YAML::BeginMap;
        for (const auto& stack : doc.connection_stacks) {
            out << YAML::Key << stack.id << YAML::Value << YAML::BeginMap;
            if (!stack.description.empty()) {
                out << YAML::Key << "description" << YAML::Value << stack.description;
            }
            if (!stack.layers.empty()) {
                out << YAML::Key << "layers" << YAML::Value << YAML::BeginSeq;
                for (const auto& layer : stack.layers) {
                    out << YAML::Flow << YAML::BeginMap;
                    out << YAML::Key << "name" << YAML::Value << layer.name;
                    out << YAML::Key << "material" << YAML::Value << layer.material;
                    out << YAML::Key << "height" << YAML::Value << layer.height;
                    out << YAML::Key << "diameter" << YAML::Value << layer.diameter;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndMap;
    }

    // Components.
    if (!doc.components.empty()) {
        out << YAML::Key << "components" << YAML::Value << YAML::BeginSeq;
        for (const auto& comp : doc.components) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << comp.id;
            out << YAML::Key << "type" << YAML::Value << comp.type;
            if (comp.anchor) {
                out << YAML::Key << "anchor" << YAML::Value << comp.anchor.value();
            }
            if (!comp.technology.empty())
                out << YAML::Key << "technology" << YAML::Value << comp.technology;
            if (!comp.connection.empty())
                out << YAML::Key << "connection" << YAML::Value << comp.connection;
            if (!comp.layout.empty())
                out << YAML::Key << "layout" << YAML::Value << comp.layout;

            if (!comp.cells.empty()) {
                if (comp.cells.size() == 1) {
                    out << YAML::Key << "top_cell" << YAML::Value << comp.cells[0];
                } else {
                    out << YAML::Key << "cells" << YAML::Value << YAML::Flow
                        << YAML::BeginSeq;
                    for (const auto& cell : comp.cells) out << cell;
                    out << YAML::EndSeq;
                }
            }

            const auto& pos = comp.position;
            if (pos.x != 0 || pos.y != 0 || pos.z != 0) {
                out << YAML::Key << "position" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << pos.x;
                out << YAML::Key << "y" << YAML::Value << pos.y;
                out << YAML::Key << "z" << YAML::Value << pos.z;
                out << YAML::EndMap;
            }

            if (comp.rotation.z != 0) {
                out << YAML::Key << "rotation" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "z" << YAML::Value << comp.rotation.z;
                out << YAML::EndMap;
            }

            if (!comp.orientation.empty() && comp.orientation != "face_up") {
                out << YAML::Key << "orientation" << YAML::Value << comp.orientation;
            }

            const auto& dims = comp.dimensions;
            if (dims.width != 0 || dims.height != 0 || dims.thickness != 0) {
                out << YAML::Key << "dimensions" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "width" << YAML::Value << dims.width;
                out << YAML::Key << "height" << YAML::Value << dims.height;
                out << YAML::Key << "thickness" << YAML::Value << dims.thickness;
                out << YAML::EndMap;
            }

            // Emitted only when engaged, so files without an attachment surface
            // round-trip byte-identically (the legacy thickness-as-mount case).
            if (comp.attachment_surface_z) {
                out << YAML::Key << "attachment_surface_z" << YAML::Value
                    << comp.attachment_surface_z.value();
            }

            if (comp.array) {
                const auto& arr = comp.array.value();
                out << YAML::Key << "array" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "pattern" << YAML::Value << arr.pattern;
                out << YAML::Key << "count" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.count_x;
                out << YAML::Key << "y" << YAML::Value << arr.count_y;
                out << YAML::EndMap;
                out << YAML::Key << "pitch" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.pitch_x;
                out << YAML::Key << "y" << YAML::Value << arr.pitch_y;
                out << YAML::EndMap;
                out << YAML::Key << "start_position" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.start_position.x;
                out << YAML::Key << "y" << YAML::Value << arr.start_position.y;
                out << YAML::Key << "z" << YAML::Value << arr.start_position.z;
                out << YAML::EndMap;
                out << YAML::EndMap;
            }

            if (!comp.metadata.empty()) {
                out << YAML::Key << "metadata" << YAML::Value << YAML::BeginMap;
                for (const auto& [key, value] : comp.metadata) {
                    out << YAML::Key << key << YAML::Value << value;
                }
                out << YAML::EndMap;
            }

            if (!comp.io_pads.empty()) {
                out << YAML::Key << "io_pads" << YAML::Value << YAML::BeginSeq;
                for (const auto& pad : comp.io_pads) {
                    out << YAML::BeginMap;
                    out << YAML::Key << "id" << YAML::Value << pad.id;
                    out << YAML::Key << "io_class" << YAML::Value << pad.io_class;
                    if (!pad.net.empty())
                        out << YAML::Key << "net" << YAML::Value << pad.net;
                    out << YAML::Key << "position" << YAML::Value << YAML::Flow
                        << YAML::BeginMap;
                    out << YAML::Key << "x" << YAML::Value << pad.pos_x;
                    out << YAML::Key << "y" << YAML::Value << pad.pos_y;
                    out << YAML::EndMap;
                    out << YAML::Key << "size" << YAML::Value << YAML::Flow
                        << YAML::BeginMap;
                    out << YAML::Key << "x" << YAML::Value << pad.size_x;
                    out << YAML::Key << "y" << YAML::Value << pad.size_y;
                    out << YAML::EndMap;
                    if (!pad.layer.empty())
                        out << YAML::Key << "layer" << YAML::Value << pad.layer;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    // Interfaces.
    if (!doc.interfaces.empty()) {
        out << YAML::Key << "interfaces" << YAML::Value << YAML::BeginSeq;
        for (const auto& iface : doc.interfaces) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << iface.id;
            out << YAML::Key << "type" << YAML::Value << iface.type;
            if (iface.from) {
                out << YAML::Key << "from" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "component" << YAML::Value << iface.from->component;
                out << YAML::Key << "surface" << YAML::Value << iface.from->surface;
                out << YAML::Key << "port_layer" << YAML::Value << iface.from->port_layer;
                out << YAML::EndMap;
            }
            if (iface.to) {
                out << YAML::Key << "to" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "component" << YAML::Value << iface.to->component;
                out << YAML::Key << "surface" << YAML::Value << iface.to->surface;
                out << YAML::Key << "port_layer" << YAML::Value << iface.to->port_layer;
                out << YAML::EndMap;
            }
            if (iface.physical) {
                out << YAML::Key << "physical" << YAML::Value << YAML::Flow
                    << YAML::BeginMap;
                out << YAML::Key << "pitch" << YAML::Value << iface.physical->pitch;
                out << YAML::Key << "diameter" << YAML::Value << iface.physical->diameter;
                out << YAML::Key << "height" << YAML::Value << iface.physical->height;
                out << YAML::EndMap;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    // Netlist.
    if (doc.netlist.present &&
        (!doc.netlist.nets.empty() || !doc.netlist.external_netlist.empty())) {
        out << YAML::Key << "netlist" << YAML::Value << YAML::BeginMap;
        if (!doc.netlist.nets.empty()) {
            out << YAML::Key << "nets" << YAML::Value << YAML::BeginSeq;
            for (const auto& net : doc.netlist.nets) {
                out << YAML::BeginMap;
                out << YAML::Key << "name" << YAML::Value << net.name;
                out << YAML::Key << "class" << YAML::Value << net.net_class;
                if (net.external) {
                    out << YAML::Key << "external" << YAML::Value << net.external;
                }
                if (!net.connections.empty()) {
                    out << YAML::Key << "connections" << YAML::Value << YAML::BeginSeq;
                    for (const auto& conn : net.connections) {
                        out << YAML::Flow << YAML::BeginMap;
                        out << YAML::Key << "component" << YAML::Value << conn.component;
                        out << YAML::Key << "pin" << YAML::Value << conn.pin;
                        if (!conn.layer.empty()) {
                            out << YAML::Key << "layer" << YAML::Value << conn.layer;
                        }
                        out << YAML::EndMap;
                    }
                    out << YAML::EndSeq;
                }
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }
        if (!doc.netlist.external_netlist.empty()) {
            out << YAML::Key << "external_netlist" << YAML::Value
                << doc.netlist.external_netlist;
        }
        out << YAML::EndMap;
    }

    // Flow block (preserved verbatim).
    if (doc.has_flow && !doc.flow_yaml.empty()) {
        YAML::Node flowNode = YAML::Load(doc.flow_yaml);
        out << YAML::Key << "flow" << YAML::Value << flowNode;
    }

    out << YAML::EndMap;

    return std::string(out.c_str()) + "\n";
}

void dump(const ChipletDocument& doc, const std::string& path,
          const DumpOptions& opts) {
    const std::string text = dumps(doc, opts);
    std::ofstream file(path);
    if (!file.is_open()) {
        throw ChipletFormatError("could not open file for writing: " + path);
    }
    file << text;
}

}  // namespace chiplet_format_io
