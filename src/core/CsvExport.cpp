#include "CsvExport.hpp"
#include <cstdio>

namespace scinodes {

bool writeSinkCsv(const std::string& path,
                  const std::vector<float>& buf, int wIdx,
                  float latestTime, float dt,
                  const std::string& nodeLabel,
                  std::string* outError) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        if (outError)
            *outError = "Could not open '" + path + "' for writing.";
        return false;
    }

    if (!nodeLabel.empty())
        std::fprintf(f, "# Sink: %s\n", nodeLabel.c_str());
    std::fprintf(f, "time,value\n");

    const int N = static_cast<int>(buf.size());
    if (N > 0 && wIdx > 0) {
        const int count = (wIdx < N) ? wIdx : N;
        for (int i = 0; i < count; ++i) {
            int ringIdx = ((wIdx - count + i) % N + N) % N;
            float t = latestTime - static_cast<float>(count - 1 - i) * dt;
            std::fprintf(f, "%.6f,%.9g\n",
                         static_cast<double>(t),
                         static_cast<double>(buf[ringIdx]));
        }
    }

    std::fclose(f);
    return true;
}

}  // namespace scinodes
