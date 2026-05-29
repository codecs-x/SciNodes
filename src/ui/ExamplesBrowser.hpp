#pragma once

#include "../core/LinearExampleLibrary.hpp"

#include <memory>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// ExamplesBrowser — ventana "Help > Examples" con search arriba, lista de
// ejemplos a la izquierda y descripción + Load a la derecha.
//
// El descubrimiento y el ranking viven detrás de `IExampleLibrary`
// (`LinearExampleLibrary` por ahora; futuro: FTS5 sobre miles de
// entradas).  Este panel sólo dibuja — no parsea JSON, no compara
// substrings, no decide ranking.  Es un consumer puro del backend.
//
// Cada `.scn` lleva su propia metadata embebida (`title`, `description`,
// `tags`, `id`) — no hay manifest centralizado.  Es el principio de
// documentación descentralizada aplicado al catálogo de ejemplos.
//
// Uso:
//   1. AppWindow tiene un miembro `ExamplesBrowser m_examples`.
//   2. El menú Help dispara `m_examples.open()`.
//   3. Cada frame se llama `m_examples.draw()`; si el usuario presionó Load,
//      el método devuelve true y `pickedPath()` da el path absoluto a abrir.
// -----------------------------------------------------------------------------
class ExamplesBrowser {
public:
    // Acción solicitada por el usuario en el frame actual.  El caller la
    // traduce: Load → reemplazar grafo actual (gateado por
    // "unsaved changes"); Import → merge sin destruir.
    enum class Action { None, Load, Import };

    ExamplesBrowser() = default;

    void open()  { m_open = true; m_needsLoad = true; }
    void close() { m_open = false; }

    // Renderiza la ventana cuando está abierta.  Devuelve la acción
    // solicitada por el usuario; si != None, `pickedPath()` da el path
    // absoluto al archivo elegido (válido sólo durante este frame).
    Action draw();

    // Path absoluto del .scn elegido en la última acción no-None.
    const std::string& pickedPath() const { return m_pickedPath; }

private:
    void rebuildLibrary();
    void recomputeHits();

    bool        m_open       = false;
    bool        m_needsLoad  = false;
    std::string m_loadError;          // mensaje si el directorio no se encuentra

    std::unique_ptr<scinodes::LinearExampleLibrary> m_library;
    std::vector<scinodes::SearchHit> m_hits;     // resultado del último search
    int         m_selected   = -1;    // índice en m_hits
    std::string m_selectedPath;       // path absoluto del seleccionado — se
                                      // usa para preservar la selección a
                                      // través de re-búsquedas que cambian
                                      // el orden/cantidad de m_hits.

    char        m_searchBuf[128] = {};
    std::string m_pickedPath;
    std::string m_examplesDir;        // resuelto al abrir, para el footer
};
