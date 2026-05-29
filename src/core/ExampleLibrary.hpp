#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// IExampleLibrary — contrato para descubrir y buscar grafos de ejemplo
// (`.scn`) tratados como artefactos de investigación.
//
// El propósito de esta abstracción es separar dos preguntas que el editor
// hace al filesystem y que tienden a entrelazarse:
//
//   1. «¿Qué ejemplos hay?»          → refresh() + entries()
//   2. «¿Cuál se parece a esto?»     → search(query)
//
// El primer backend (`LinearExampleLibrary`) escanea un directorio y
// rankea con heurísticas obvias.  Para una biblioteca de cientos o miles
// de entradas vale la pena cambiar a un backend con índice invertido o
// FTS5; ese swap no debe tocar la UI ni el resto del modelo, por eso
// vive detrás de esta interfaz — es el mismo patrón que `IComputeBackend`
// frente a los solvers.
//
// Notas de diseño:
//   - Las `ExampleEntry` viajan por valor: `SearchHit::entry` es una copia,
//     no un puntero dentro del backend.  Si el backend hace `refresh()`
//     justo después de un `search()`, los hits siguen siendo válidos.
//   - `score` es no-negativo; el orden absoluto importa más que la
//     magnitud.  La escala es definida por el backend (LinearExampleLibrary
//     suma pesos por tipo de campo, otros podrían normalizar 0..1) —
//     la UI sólo se compromete a "mayor → más relevante".
//   - `snippet` puede incluir marcadores `**...**` alrededor del término
//     que matcheó, para que la UI pueda renderizarlos resaltados sin
//     re-correr el matcher.
// -----------------------------------------------------------------------------
namespace scinodes {

// Una entrada de la biblioteca — corresponde a un archivo .scn en disco.
// Los campos derivan del header del .scn (root metadata) o, en transición,
// de un manifest sidecar (index.json).  La conversión es responsabilidad
// del backend; el resto del editor sólo ve `ExampleEntry`.
struct ExampleEntry {
    std::filesystem::path    file;         // ruta absoluta al .scn
    std::string              id;           // estable: stem del filename por default
    std::string              title;        // legible — fallback al stem si vacío
    std::string              description;  // texto libre, puede tener \n
    std::vector<std::string> tags;         // libres (case-sensitive de almacenamiento,
                                            // case-insensitive de búsqueda)
};

// Una consulta — texto libre + facetas booleanas sobre tags.
// Backends linealmente simples ignoran `limit` o lo respetan trivialmente;
// backends FTS lo usan para paginar resultados sin materializar todos.
struct SearchQuery {
    std::string              text;         // case-insensitive, partido por espacios
    std::vector<std::string> mustTags;     // AND — la entry debe tener todos
    std::vector<std::string> anyTags;      // OR  — la entry debe tener al menos uno
    std::size_t              limit = 0;    // 0 = sin límite
};

// Un resultado — la entrada que matcheó, su score y dónde matcheó.
struct SearchHit {
    ExampleEntry             entry;
    float                    score = 0.0f;
    // Lista de campos donde se encontró el texto, en formato:
    //   "title", "description", "id", "tag:<tag literal>"
    // La UI puede usarlo para indicar al usuario por qué un resultado
    // aparece (estilo Obsidian: "matched in tags + description").
    std::vector<std::string> matchedFields;
    // Fragmento de description con contexto alrededor del primer match,
    // delimitando la coincidencia con `**...**`.  Vacío si el match no
    // fue en description o si description está vacía.
    std::string              snippet;
};

// El contrato propiamente.  Implementaciones deben ser thread-safe para
// `search()` y `entries()` const tras un `refresh()` exitoso; `refresh()`
// puede no serlo (lo llama el dueño en el thread de UI).
class IExampleLibrary {
public:
    virtual ~IExampleLibrary() = default;

    // Re-escanea la fuente subyacente.  Idempotente.  Tras esta llamada,
    // `entries()`, `allTags()`, `search()` reflejan el nuevo estado.
    // En caso de error parcial (algún archivo ilegible) el backend
    // continúa y reporta vía `lastErrors()` o saltándose esa entrada.
    virtual void refresh() = 0;

    // Vista de todas las entradas conocidas, en el orden en que el
    // backend las cargó.  No incluye ranking — para eso `search()`.
    virtual const std::vector<ExampleEntry>& entries() const = 0;

    // Tags únicos (deduplicados, ordenados alfabéticamente) de todas
    // las entradas.  Para la UI de autocompletar tags al buscar.
    virtual std::vector<std::string> allTags() const = 0;

    // Búsqueda ranqueada.  Si `query.text` está vacío y `mustTags` y
    // `anyTags` también, devuelve todas las entradas con score 0.
    virtual std::vector<SearchHit> search(const SearchQuery& query) const = 0;

    // Acceso por path absoluto — para "abrir el último ejemplo" o
    // navegar a un grafo desde una URL.  Devuelve nullptr si no existe.
    virtual const ExampleEntry*
    findByPath(const std::filesystem::path& file) const = 0;
};

}  // namespace scinodes
