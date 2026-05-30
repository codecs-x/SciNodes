// Test headless de i18n.
//
// Convención del proyecto (post-refactor):
//   • El inglés vive en el código C++ (NodeDef::label, ::description,
//     menú strings, etc.).  No hay i18n/en.json — load("en") solo
//     limpia la tabla y tr() cae al fallback def.label.
//   • Las traducciones a otros idiomas viven en i18n/<lang>.json.  La
//     única requerida hoy es es.json.
//
// Este test verifica:
//   1. load("en") devuelve éxito, deja la tabla vacía y tr() retorna
//      la clave misma (= comportamiento del fallback en la app real).
//   2. load("es") carga el archivo y tiene al menos algunas claves
//      conocidas.
//   3. tr() devuelve la traducción correcta cuando la clave existe,
//      y la clave misma cuando no.
//   4. trOr(key, fallback) devuelve el fallback si la clave no está.
//
// Corre desde la raíz del repo.

#include "../src/core/I18n.hpp"

#include <cstdio>

static int g_passed = 0, g_failed = 0;

static void check(bool cond, const char* msg) {
    if (cond) ++g_passed;
    else { ++g_failed; std::fprintf(stderr, "  FAIL: %s\n", msg); }
}

int main() {
    std::fprintf(stdout, "================================================================\n");
    std::fprintf(stdout, " test_i18n — i18n con inglés en C++ + traducciones en JSON\n");
    std::fprintf(stdout, "================================================================\n");

    auto& i18n = scinodes::I18n::instance();

    // ---- 1. load("en") — caso especial ---------------------------------
    bool okEn = i18n.load("en");
    check(okEn, "load(\"en\") devuelve true");
    check(i18n.currentLanguage() == "en",
          "currentLanguage() == \"en\" tras load(\"en\")");
    // Cuando no hay traducción ni fallback explícito, tr() ahora
    // deriva un default razonable del último segmento del key
    // (`menu.file` → "File").  Antes devolvía la clave cruda, lo
    // que aparecía como "menu.file" / "statusbar.run" en la UI al
    // cambiar a inglés.
    check(scinodes::tr("menu.file") == "File",
          "tr() deriva 'File' del key 'menu.file' en \"en\"");
    check(scinodes::trOr("menu.file", "File") == "File",
          "trOr() devuelve el fallback en \"en\"");

    // ---- 2. load("es") — debe encontrar i18n/es.json -------------------
    bool okEs = i18n.load("es");
    check(okEs, "load(\"es\") carga i18n/es.json");
    check(i18n.currentLanguage() == "es",
          "currentLanguage() == \"es\" tras load(\"es\")");

    // ---- 3. tr() en español -------------------------------------------
    check(scinodes::tr("menu.file") == "Archivo",
          "tr(\"menu.file\") en es devuelve \"Archivo\"");
    check(scinodes::tr("node.PIDController.label") == "Controlador PID",
          "tr(\"node.PIDController.label\") en es");
    // El último segmento se deriva con underscore→espacio + title
    // case del primer char de cada palabra.  Para `__nonexistent__`
    // (no es un key i18n real) el comportamiento sigue siendo
    // razonable: " Nonexistent " (subrayas iniciales/finales quedan
    // como espacios).
    check(scinodes::tr("__nonexistent__") == "  Nonexistent  ",
          "tr() deriva default razonable para clave inexistente");

    // ---- 4. trOr() ----------------------------------------------------
    check(scinodes::trOr("menu.file", "FALLBACK") == "Archivo",
          "trOr() devuelve la traducción si existe");
    check(scinodes::trOr("__nope__", "FALLBACK") == "FALLBACK",
          "trOr() devuelve el fallback si no existe la clave");

    // ---- 5. availableLanguages() siempre incluye "en" -----------------
    auto langs = i18n.availableLanguages();
    bool hasEn = false, hasEs = false;
    for (const auto& l : langs) {
        if (l == "en") hasEn = true;
        if (l == "es") hasEs = true;
    }
    check(hasEn, "availableLanguages() incluye \"en\" (siempre)");
    check(hasEs, "availableLanguages() incluye \"es\" (encontrado i18n/es.json)");

    std::fprintf(stdout, "\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
