#include "CsvParamIO.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace scinodes {

namespace {

// Trim surrounding ASCII whitespace.
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Split on the first N commas; preserves quoted-string content as a single
// field. Param names contain "Sample Rate", "num[0]", etc. — never commas —
// so a permissive splitter is fine.
std::vector<std::string> splitCsv(const std::string& line, int maxFields) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (c == ',' && !inQuotes &&
            (maxFields <= 0 || (int)out.size() < maxFields - 1)) {
            out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(trim(cur));
    return out;
}

}  // namespace

bool writeNodeParamsCsv(const std::string& path,
                        const NodeInstance& node,
                        const std::string& nodeLabel,
                        std::string* outError) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        if (outError) *outError = "Could not open '" + path + "' for writing.";
        return false;
    }
    if (!nodeLabel.empty())
        std::fprintf(f, "# SciNodes parameters for %s\n", nodeLabel.c_str());
    std::fprintf(f, "parameter,value,units\n");

    // Emit in the order defined by the node's registry entry so the
    // exported file is stable across runs — NodeInstance::params is an
    // unordered_map, but defOf(...)::params preserves declaration order.
    const auto& def = defOf(node);
    for (const auto& pd : def.params) {
        auto it = node.params.find(pd.name);
        double v = (it != node.params.end()) ? it->second : pd.defaultValue;
        std::fprintf(f, "%s,%.10g,%s\n",
                     pd.name.c_str(), v, pd.unit.c_str());
    }
    std::fclose(f);
    return true;
}

bool readNodeParamsCsv(const std::string& path,
                       NodeInstance& node,
                       std::string* outError,
                       std::vector<std::string>* outWarnings) {
    std::ifstream f(path);
    if (!f) {
        if (outError) *outError = "Could not open '" + path + "' for reading.";
        return false;
    }

    bool sawHeader = false;
    std::vector<std::pair<std::string, double>> applied;
    int lineNo = 0;
    for (std::string raw; std::getline(f, raw); ) {
        ++lineNo;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;

        auto fields = splitCsv(line, 3);
        if (!sawHeader) {
            // First non-comment, non-empty line must be the header.
            if (fields.size() < 2 || fields[0] != "parameter" ||
                fields[1] != "value") {
                if (outError) {
                    *outError = "Missing or malformed CSV header on line "
                              + std::to_string(lineNo) +
                              " (expected: parameter,value[,units]).";
                }
                return false;
            }
            sawHeader = true;
            continue;
        }

        if (fields.size() < 2) {
            if (outError) {
                *outError = "Truncated row on line " + std::to_string(lineNo)
                          + ": '" + raw + "'.";
            }
            return false;
        }

        double v = 0.0;
        try {
            size_t consumed = 0;
            v = std::stod(fields[1], &consumed);
            if (consumed == 0) throw std::invalid_argument("not a number");
        } catch (const std::exception&) {
            if (outError) {
                *outError = "Non-numeric value on line "
                          + std::to_string(lineNo) +
                          ": '" + fields[1] + "'.";
            }
            return false;
        }
        applied.emplace_back(fields[0], v);
    }

    if (!sawHeader) {
        if (outError) *outError = "CSV file contained no header row.";
        return false;
    }

    // Apply only after the whole file parsed cleanly. Unknown params go
    // to the warning list rather than aborting — partial spreadsheets
    // are a valid use case.
    const auto& def = defOf(node);
    for (const auto& [name, value] : applied) {
        bool known = false;
        for (const auto& pd : def.params)
            if (pd.name == name) { known = true; break; }
        if (!known) {
            if (outWarnings)
                outWarnings->push_back("Unknown parameter '" + name +
                                       "' — ignored.");
            continue;
        }
        node.params[name] = value;
    }
    return true;
}

}  // namespace scinodes
