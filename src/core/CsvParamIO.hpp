#pragma once
#include "NodeInstance.hpp"
#include <string>

// -----------------------------------------------------------------------
// CsvParamIO — round-trip a node's parameter block to/from a CSV file.
//
// File format:
//   # SciNodes parameters for <typeName>   (optional comment header)
//   parameter,value,units
//   Target Torque,10,Nm
//   Target Speed,150,rad/s
//   ...
//
// Rules:
//   • Lines starting with '#' are comments and skipped on import.
//   • The header row "parameter,value,units" is required; the units
//     column is optional in the file (export always emits it).
//   • Import matches the parameter column to NodeInstance::params by
//     exact name. Unknown parameter names are accumulated in `outWarnings`
//     but do not abort the load.
//   • Missing parameters from the file are left at their current value,
//     so a partial spreadsheet (e.g. only the rows the user wants to
//     change) is a valid input.
//
// Used by the param panel's Import / Export buttons. The "requirements
// block" use case (DesignTemplate spreadsheet round-trip) is the primary
// motivation, but the helpers work uniformly with every node type.
// -----------------------------------------------------------------------
namespace scinodes {

// Writes the node's params to `path`. The optional `nodeLabel` becomes
// the # comment header. Returns true on success; on failure, *outError
// (if non-null) is filled with a human-readable message.
bool writeNodeParamsCsv(const std::string& path,
                        const NodeInstance& node,
                        const std::string& nodeLabel = "",
                        std::string* outError = nullptr);

// Reads `path` and applies the parameters to `node` in place. Returns
// true on success. `outWarnings` (optional, append-only) collects
// unknown-parameter names. Format errors set *outError and return false
// without mutating `node`.
bool readNodeParamsCsv(const std::string& path,
                       NodeInstance& node,
                       std::string* outError = nullptr,
                       std::vector<std::string>* outWarnings = nullptr);

}  // namespace scinodes
