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

// -----------------------------------------------------------------------
// Export "todos los sinks" — dos modos.
//
// SinkExport: descripción auto-contenida de un sink para la exportación.
// Una entrada por sink; los sinks multi-canal (Oscilloscope con varios
// puertos, PhasePortrait con x/y) tienen varias entradas en `channels`.
// -----------------------------------------------------------------------
struct SinkChannel {
    std::vector<float> buf;
    int                writeIndex = 0;
    // Nombre legible de la columna en el CSV ancho — típicamente
    // "<nodeLabel>_ch<i>" o, si el sink tiene un portLabel custom (caso
    // Oscilloscope), ese label tal cual.
    std::string        columnHeader;
};

struct SinkExport {
    int                       nodeId;
    std::string               label;
    std::vector<SinkChannel>  channels;   // ≥ 1
};

// Modo 1 — un único CSV ancho: una columna `time` y una columna por
// canal de cada sink.  Todos los sinks comparten el mismo eje temporal
// (latestTime, dt globales).  Si las longitudes de ring buffer difieren
// entre canales, la columna del canal más corto se rellena con celdas
// vacías en las filas iniciales.  Más cómodo para importar a Excel /
// pandas / GNU plot.
bool writeAllSinksWide(const std::string& path,
                       const std::vector<SinkExport>& sinks,
                       float latestTime, float dt,
                       std::string* outError = nullptr);

// Modo 2 — un CSV por sink dentro de `folderPath`.  Genera archivos
// `sink_<nodeId>_<label>.csv` con columnas `time, ch0, ch1, ...`.  Mejor
// para diff/grep o cuando hay muchísimos canales.  Crea el directorio
// si no existe.
bool writeAllSinksFolder(const std::string& folderPath,
                         const std::vector<SinkExport>& sinks,
                         float latestTime, float dt,
                         std::string* outError = nullptr);

}  // namespace scinodes
