#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace scinodes {

// -----------------------------------------------------------------------
// I18n — internacionalización mínima por JSON plano.
//
// Cada idioma vive en `i18n/<lang>.json` con claves planas separadas por
// punto: `{"menu.file.new": "Nuevo", "menu.file.open": "Abrir…", ...}`.
// El proyecto entero consulta vía `tr("menu.file.new")` (la función libre
// de abajo); si la clave no está en el idioma activo, se devuelve la
// clave misma — sin crash, sin string vacío, con feedback visual de qué
// falta por traducir.
//
// Diseño:
//   • Singleton vía `instance()` — la traducción es estado de UI global,
//     accedido desde todos los paneles.  El patrón Service Locator
//     ya estaba documentado para `customNodes()` (Cap. 6 de la tesis).
//   • Switch en runtime via `load(lang)`.  No hay restart, no hay
//     reflow forzado; ImGui re-llama `tr()` cada frame y los textos se
//     actualizan al siguiente render.
//   • Default `es` (override por env `SCINODES_LANG`).
// -----------------------------------------------------------------------
class I18n {
public:
    static I18n& instance();

    // Carga el JSON `i18n/<langCode>.json` (relativo al cwd; busca
    // también en `../i18n/` para que el binario corra desde `build/`).
    // Si el archivo no existe, deja la tabla vacía y todas las
    // llamadas a tr devolverán la clave.  Devuelve true si cargó
    // algún string.
    bool load(const std::string& langCode);

    // Lookup.  Si la clave no existe, devuelve la clave misma.
    const std::string& tr(const std::string& key) const;

    // Variante con fallback explícito: si la clave no está, devuelve
    // `fallback` (típicamente el label C++ existente).  Útil para
    // migración incremental — añadir traducciones sin romper nada si
    // todavía no se traducen todos los strings.
    const std::string& trOr(const std::string& key,
                            const std::string& fallback) const;

    const std::string& currentLanguage() const { return m_lang; }

    // Lista los idiomas disponibles examinando `i18n/*.json` en disco.
    // Pensado para el menú "View → Language".
    std::vector<std::string> availableLanguages() const;

private:
    I18n() = default;

    std::unordered_map<std::string, std::string> m_strings;
    std::string                                  m_lang;
    // Cache de fallbacks derivados (último segmento del key con
    // underscore→espacio y title-case) para que tr() siga devolviendo
    // un `const string&` estable cuando no hay traducción ni
    // fallback explícito.  Se llena lazily en tr() y nunca crece más
    // allá del número de keys consultados sin traducción.
    mutable std::unordered_map<std::string, std::string> m_derivedCache;
};

// Helper terse — el 99% del código de UI solo necesita esto.
inline const std::string& tr(const std::string& key) {
    return I18n::instance().tr(key);
}
inline const std::string& trOr(const std::string& key,
                               const std::string& fallback) {
    return I18n::instance().trOr(key, fallback);
}

}  // namespace scinodes
