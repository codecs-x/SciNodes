#include "CsvExport.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace scinodes {

namespace {

// Convierte el ring buffer del canal a un vector temporal en orden
// cronológico (más viejo → más nuevo).  Devuelve [count] valores donde
// count = min(writeIndex, N).
std::vector<float> chronological(const SinkChannel& ch) {
    const int N = static_cast<int>(ch.buf.size());
    std::vector<float> out;
    if (N == 0 || ch.writeIndex <= 0) return out;
    const int count = std::min(ch.writeIndex, N);
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int ringIdx =
            ((ch.writeIndex - count + i) % N + N) % N;
        out.push_back(ch.buf[ringIdx]);
    }
    return out;
}

}  // namespace

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

// ===========================================================================
// Export "todos los sinks"
// ===========================================================================
bool writeAllSinksWide(const std::string& path,
                       const std::vector<SinkExport>& sinks,
                       float latestTime, float dt,
                       std::string* outError) {
    // 1) Linealizar cada canal y encontrar el conteo máximo (todos
    //    los sinks comparten el mismo eje temporal, así que el
    //    canal con más samples define la longitud del CSV).
    struct LinChannel {
        std::vector<float> data;
        std::string        header;
    };
    std::vector<LinChannel> channels;
    int maxCount = 0;
    for (const SinkExport& s : sinks) {
        for (const SinkChannel& ch : s.channels) {
            LinChannel lc;
            lc.data   = chronological(ch);
            lc.header = ch.columnHeader.empty()
                ? (s.label + "_ch" + std::to_string(channels.size()))
                : ch.columnHeader;
            if (static_cast<int>(lc.data.size()) > maxCount)
                maxCount = static_cast<int>(lc.data.size());
            channels.push_back(std::move(lc));
        }
    }

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        if (outError) *outError = "Could not open '" + path + "' for writing.";
        return false;
    }

    // 2) Header.
    std::fprintf(f, "time");
    for (const LinChannel& lc : channels)
        std::fprintf(f, ",%s", lc.header.c_str());
    std::fprintf(f, "\n");

    // 3) Filas.  Canales más cortos quedan en blanco en sus primeras
    //    filas (cuando aún no tenían datos).
    for (int i = 0; i < maxCount; ++i) {
        const float t = latestTime - static_cast<float>(maxCount - 1 - i) * dt;
        std::fprintf(f, "%.6f", static_cast<double>(t));
        for (const LinChannel& lc : channels) {
            const int cnt = static_cast<int>(lc.data.size());
            const int chOffset = maxCount - cnt;
            if (i < chOffset) {
                std::fprintf(f, ",");
            } else {
                std::fprintf(f, ",%.9g",
                             static_cast<double>(lc.data[i - chOffset]));
            }
        }
        std::fprintf(f, "\n");
    }

    std::fclose(f);
    return true;
}

bool writeAllSinksFolder(const std::string& folderPath,
                         const std::vector<SinkExport>& sinks,
                         float latestTime, float dt,
                         std::string* outError) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(folderPath, ec);
    if (ec) {
        if (outError)
            *outError = "Could not create '" + folderPath +
                        "': " + ec.message();
        return false;
    }

    for (const SinkExport& s : sinks) {
        // Nombre del archivo: sink_<id>_<label>.csv.  Sanitizamos el
        // label removiendo separadores que rompen rutas.
        std::string safeLabel = s.label;
        for (char& c : safeLabel) {
            if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
        }
        const std::string filePath =
            folderPath + "/sink_" + std::to_string(s.nodeId) +
            "_" + safeLabel + ".csv";

        FILE* f = std::fopen(filePath.c_str(), "w");
        if (!f) {
            if (outError)
                *outError = "Could not open '" + filePath + "' for writing.";
            return false;
        }

        // Linealizar todos los canales del sink primero (para encabezado
        // y para iterar fila por fila).
        std::vector<std::vector<float>> lin;
        std::vector<std::string>        headers;
        int maxCount = 0;
        for (size_t i = 0; i < s.channels.size(); ++i) {
            lin.push_back(chronological(s.channels[i]));
            headers.push_back(s.channels[i].columnHeader.empty()
                ? ("ch" + std::to_string(i))
                : s.channels[i].columnHeader);
            if (static_cast<int>(lin.back().size()) > maxCount)
                maxCount = static_cast<int>(lin.back().size());
        }

        std::fprintf(f, "# Sink: %s (id=%d)\n", s.label.c_str(), s.nodeId);
        std::fprintf(f, "time");
        for (const std::string& h : headers)
            std::fprintf(f, ",%s", h.c_str());
        std::fprintf(f, "\n");

        for (int i = 0; i < maxCount; ++i) {
            const float t = latestTime -
                            static_cast<float>(maxCount - 1 - i) * dt;
            std::fprintf(f, "%.6f", static_cast<double>(t));
            for (size_t c = 0; c < lin.size(); ++c) {
                const int cnt = static_cast<int>(lin[c].size());
                const int chOffset = maxCount - cnt;
                if (i < chOffset) std::fprintf(f, ",");
                else std::fprintf(f, ",%.9g",
                                  static_cast<double>(lin[c][i - chOffset]));
            }
            std::fprintf(f, "\n");
        }
        std::fclose(f);
    }
    return true;
}

}  // namespace scinodes
