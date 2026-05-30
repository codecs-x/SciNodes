#pragma once

#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// ExamplesBrowser — ventana "Help > Examples" con search arriba, lista de
// ejemplos a la izquierda y descripción + Load a la derecha.
//
// El catálogo vive en examples/graphs/index.json y se carga perezosamente la
// primera vez que la ventana se abre.  Cada entrada tiene id, title, file,
// tags, description; el filtro de búsqueda compara case-insensitive contra
// title, id y tags.
//
// Uso:
//   1. AppWindow tiene un miembro `ExamplesBrowser m_examples`.
//   2. El menú Help dispara `m_examples.open()`.
//   3. Cada frame se llama `m_examples.draw()`; si el usuario presionó Load,
//      el método devuelve true y `pickedPath()` da el path absoluto a abrir.
// -----------------------------------------------------------------------------
class ExamplesBrowser {
public:
    ExamplesBrowser() = default;

    void open()  { m_open = true; m_needsLoad = true; }
    void close() { m_open = false; }

    // Renderiza la ventana cuando está abierta.  Devuelve true durante el
    // frame en que el usuario presionó Load — el caller debe leer
    // pickedPath() y cargar el archivo.
    bool draw();

    // Path absoluto del .scn elegido en el último Load (válido solo en el
    // frame en que draw() devolvió true).
    const std::string& pickedPath() const { return m_pickedPath; }

private:
    struct Entry {
        std::string id;
        std::string title;
        std::string file;             // ruta relativa al directorio de examples
        std::string description;
        std::vector<std::string> tags;
    };

    void loadManifest();
    bool matchesSearch(const Entry& e) const;

    bool        m_open       = false;
    bool        m_needsLoad  = false;
    std::string m_loadError;          // mensaje si index.json falló al cargar
    std::vector<Entry> m_entries;
    int         m_selected   = -1;    // índice en m_entries
    std::string m_examplesDir;        // directorio absoluto que contiene los .scn

    char        m_searchBuf[128] = {};
    std::string m_pickedPath;
};
