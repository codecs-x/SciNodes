#pragma once
#include "ExampleLibrary.hpp"

// -----------------------------------------------------------------------------
// LinearExampleLibrary — primer backend de `IExampleLibrary`: escanea un
// directorio en disco, lee la metadata embebida en cada `.scn` (campos
// `title`, `description`, `tags` del root), y opcionalmente complementa
// con un manifest legacy (`index.json`).  La búsqueda es lineal sobre
// todas las entradas — O(n·m) donde n = #entries y m = #tokens — con un
// ranking simple por tipo de campo donde matcheó.
//
// Estado de diseño:
//   - Pensado como puente honesto durante la migración de `index.json`
//     a metadata embebida en cada `.scn`.  Si una entry trae metadata
//     embebida, esa gana; si no, se cae al sidecar.
//   - No mantiene índice persistente.  `refresh()` re-lee todos los
//     archivos; con miles de entries esto duele y es señal para
//     escribir un `IndexedExampleLibrary` (FTS5, Tantivy, lo que sea).
//   - Sin fuzzy match, sin stemming, sin sinónimos.  Substring + tokens
//     separados por espacio.  Obsidian-light, no Obsidian.
// -----------------------------------------------------------------------------
namespace scinodes {

class LinearExampleLibrary : public IExampleLibrary {
public:
    // Construye con el directorio que será escaneado.  No llama a
    // `refresh()` por sí solo: el caller decide cuándo pagar el costo
    // de I/O (típicamente al abrir la ventana de ejemplos, no al
    // arrancar el binario).
    explicit LinearExampleLibrary(std::filesystem::path directory);

    // IExampleLibrary
    void                                     refresh() override;
    const std::vector<ExampleEntry>&         entries() const override { return m_entries; }
    std::vector<std::string>                 allTags() const override;
    std::vector<SearchHit>                   search(const SearchQuery& q) const override;
    const ExampleEntry*                      findByPath(const std::filesystem::path& f) const override;

    // Acceso al directorio raíz (para diagnóstico).
    const std::filesystem::path& directory() const { return m_directory; }

private:
    std::filesystem::path     m_directory;
    std::vector<ExampleEntry> m_entries;
};

}  // namespace scinodes
