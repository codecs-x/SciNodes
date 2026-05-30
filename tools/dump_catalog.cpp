// dump_catalog — introspecta nodeRegistry() en runtime y emite JSON.
// Sirve para verificar que la BD de doc/db/node_types.json coincide con
// lo que el binario realmente expone, no con lo que el regex parsea del
// .cpp. Es la segunda vista de la triangulación (parse → link → run).
#include "core/NodeType.hpp"
#include <iostream>
#include <iomanip>

static const char* categoryName(NodeCategory c) {
    switch (c) {
        case NodeCategory::Source:      return "Source";
        case NodeCategory::Transformer: return "Transformer";
        case NodeCategory::Device:      return "Device";
        case NodeCategory::Sink:        return "Sink";
    }
    return "?";
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out.push_back(c);
    }
    return out;
}

int main() {
    const auto& reg = nodeRegistry();
    std::cout << "{\n  \"rows\": [\n";
    bool first = true;
    for (const auto& [type, def] : reg) {
        if (!first) std::cout << ",\n";
        first = false;
        std::cout << "    {\"type\": \"" << jsonEscape(typeName(type)) << "\","
                  << " \"category\": \"" << categoryName(def.category) << "\","
                  << " \"label\": \"" << jsonEscape(def.label) << "\","
                  << " \"input_ports\": " << def.inputPorts << ","
                  << " \"output_ports\": " << def.outputPorts << ","
                  << " \"params\": [";
        for (size_t i = 0; i < def.params.size(); ++i) {
            const auto& p = def.params[i];
            if (i) std::cout << ", ";
            std::cout << "{\"name\": \"" << jsonEscape(p.name) << "\", "
                      << "\"default\": " << p.defaultValue << ", "
                      << "\"unit\": \"" << jsonEscape(p.unit) << "\"}";
        }
        std::cout << "]}";
    }
    std::cout << "\n  ]\n}\n";
    return 0;
}
