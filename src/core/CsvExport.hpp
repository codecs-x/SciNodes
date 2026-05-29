#pragma once
#include <string>
#include <vector>

// -----------------------------------------------------------------------
// CsvExport — write a ring-buffered sink stream to a CSV file.
//
// The ring buffer convention matches ScilabBridge:
//   • `buf` is fixed-length; the chronologically-newest sample lives at
//     index (wIdx - 1) mod N; only the first min(wIdx, N) entries are
//     considered "real" — anything beyond that is initial zero padding.
//   • `latestTime` is the simulated time at sample index (wIdx - 1).
//   • Samples are written in chronological order, oldest first.
//
// File format:
//   # Sink: <nodeLabel>            (optional, omitted when empty)
//   time,value
//   <t0>,<v0>
//   ...
//
// Returns true on success. On failure, `outError` (if non-null) is filled
// with a human-readable reason.
// -----------------------------------------------------------------------
namespace scinodes {

bool writeSinkCsv(const std::string& path,
                  const std::vector<float>& buf, int wIdx,
                  float latestTime, float dt,
                  const std::string& nodeLabel,
                  std::string* outError = nullptr);

}  // namespace scinodes
