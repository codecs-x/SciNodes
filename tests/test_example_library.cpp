// =============================================================================
// test_example_library.cpp — Tests headless de LinearExampleLibrary.
//
// Cada test arma un directorio temporal con archivos .scn minimales
// (sólo metadata, sin grafo real) y opcionalmente un index.json legacy,
// luego instancia LinearExampleLibrary y verifica:
//
//   - Discovery: archivos .scn aparecen como entries; el resto se ignora.
//   - Defaults: title vacío cae al stem del filename.
//   - Búsqueda por texto: matches en title, description, tags, id.
//   - Ranking: title > tag exacto > description > tag substring > id.
//   - Filtro AND: mustTags exige TODOS.
//   - Filtro OR:  anyTags exige AL MENOS UNO.
//   - Snippet incluye el match resaltado con **...**.
//   - allTags() devuelve únicos ordenados.
//   - findByPath() acierta y null-fallback.
//   - Puente legacy: index.json llena entries sin metadata embebida.
//   - Refresh: añadir un archivo nuevo lo descubre tras refresh().
// =============================================================================

#include "core/ExampleLibrary.hpp"
#include "core/LinearExampleLibrary.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;
using scinodes::ExampleEntry;
using scinodes::LinearExampleLibrary;
using scinodes::SearchHit;
using scinodes::SearchQuery;

namespace {

int g_pass = 0, g_fail = 0;

void expect_true(bool cond, const char* msg) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::fprintf(stderr, "[FAIL] %s\n", msg); }
}

void expect_eq_size(std::size_t a, std::size_t b, const char* msg) {
    if (a == b) { ++g_pass; }
    else        { ++g_fail; std::fprintf(stderr,
        "[FAIL] %s — got %zu, expected %zu\n", msg, a, b); }
}

void expect_eq_str(const std::string& a, const std::string& b, const char* msg) {
    if (a == b) { ++g_pass; }
    else        { ++g_fail; std::fprintf(stderr,
        "[FAIL] %s — got \"%s\", expected \"%s\"\n", msg, a.c_str(), b.c_str()); }
}

// Carpeta temporal autodestruible: pide una ruta en /tmp y borra al
// destruirse.  Sufijo aleatorio para evitar colisiones cuando varios
// tests corren en paralelo.
struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 0x7fffffff);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("scn_test_lib_" + std::to_string(stamp) + "_" +
                std::to_string(dist(gen)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);  // best effort
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// Escribe un .scn minimal: sólo metadata + nodes:[]/edges:[].  No
// representa un grafo válido — el linear backend sólo lee el header,
// así que esto basta.
void writeScn(const fs::path& file,
              const std::string& title,
              const std::string& description,
              const std::vector<std::string>& tags) {
    json j;
    j["scnodes_version"] = "0.4";
    j["nodes"] = json::array();
    j["edges"] = json::array();
    if (!title.empty())       j["title"]       = title;
    if (!description.empty()) j["description"] = description;
    if (!tags.empty())        j["tags"]        = tags;
    std::ofstream out(file);
    out << j.dump(2);
}

}  // namespace

// -----------------------------------------------------------------------------
static void test_discovers_scn_files_only() {
    TempDir td;
    writeScn(td.path / "alpha.scn", "Alpha", "first", {"a"});
    writeScn(td.path / "beta.scn",  "Beta",  "second",{"b"});
    // Ruido que la librería debe ignorar.
    std::ofstream(td.path / "ignore.txt") << "noise";
    std::ofstream(td.path / "garbage.scn") << "{ not valid json";

    LinearExampleLibrary lib(td.path);
    lib.refresh();
    expect_eq_size(lib.entries().size(), 2u,
                   "discovers 2 valid .scn, ignores .txt and broken JSON");
}

// -----------------------------------------------------------------------------
static void test_empty_title_falls_back_to_stem() {
    TempDir td;
    writeScn(td.path / "walkthrough_X.scn", "", "no title", {});

    LinearExampleLibrary lib(td.path);
    lib.refresh();
    expect_eq_size(lib.entries().size(), 1u, "one entry discovered");
    expect_eq_str(lib.entries()[0].title, "walkthrough_X",
                  "title falls back to filename stem when empty");
}

// -----------------------------------------------------------------------------
static void test_search_matches_title() {
    TempDir td;
    writeScn(td.path / "a.scn", "PID Controller", "loop control",  {"control"});
    writeScn(td.path / "b.scn", "Bode Plot",     "freq response",  {"freq"});

    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q; q.text = "pid";
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 1u, "search 'pid' returns 1 hit");
    if (!hits.empty())
        expect_eq_str(hits[0].entry.title, "PID Controller",
                      "search 'pid' matches PID Controller by title");
}

// -----------------------------------------------------------------------------
static void test_search_matches_description_with_snippet() {
    TempDir td;
    writeScn(td.path / "x.scn", "Sample",
        "A long description about anti-windup compensation in PID controllers.",
        {"control"});

    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q; q.text = "windup";
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 1u, "search 'windup' finds the entry");
    if (!hits.empty()) {
        const auto& h = hits[0];
        expect_true(h.snippet.find("**") != std::string::npos,
                    "snippet contains ** delimiters around match");
        expect_true(h.snippet.find("**windup**") != std::string::npos ||
                    h.snippet.find("**Windup**") != std::string::npos,
                    "snippet wraps the matched token");
    }
}

// -----------------------------------------------------------------------------
static void test_ranking_title_above_description() {
    TempDir td;
    writeScn(td.path / "a.scn", "Banana stand", "nothing here",   {});
    writeScn(td.path / "b.scn", "Generic",      "talks about banana foster", {});

    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q; q.text = "banana";
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 2u, "both entries match");
    if (hits.size() >= 2) {
        expect_eq_str(hits[0].entry.title, "Banana stand",
                      "title-match ranks above description-match");
    }
}

// -----------------------------------------------------------------------------
static void test_ranking_tag_exact_above_substring() {
    TempDir td;
    writeScn(td.path / "a.scn", "Title A", "desc A", {"control"});
    writeScn(td.path / "b.scn", "Title B", "desc B", {"controller"});

    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q; q.text = "control";
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 2u, "both entries match 'control'");
    if (hits.size() >= 2) {
        // El tag exacto "control" pesa más que el substring de "controller".
        expect_eq_str(hits[0].entry.title, "Title A",
                      "exact tag match outranks substring tag match");
    }
}

// -----------------------------------------------------------------------------
static void test_must_tags_AND() {
    TempDir td;
    writeScn(td.path / "a.scn", "A", "x", {"control", "ogata"});
    writeScn(td.path / "b.scn", "B", "x", {"control"});
    writeScn(td.path / "c.scn", "C", "x", {"ogata"});

    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q;
    q.mustTags = {"control", "ogata"};
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 1u, "AND filter keeps only the entry with both tags");
    if (!hits.empty())
        expect_eq_str(hits[0].entry.title, "A", "AND survivor is A");
}

// -----------------------------------------------------------------------------
static void test_any_tags_OR() {
    TempDir td;
    writeScn(td.path / "a.scn", "A", "x", {"control"});
    writeScn(td.path / "b.scn", "B", "x", {"robot"});
    writeScn(td.path / "c.scn", "C", "x", {"thermal"});

    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q;
    q.anyTags = {"control", "robot"};
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 2u, "OR filter keeps entries matching any tag");
}

// -----------------------------------------------------------------------------
static void test_all_tags_unique_sorted() {
    TempDir td;
    writeScn(td.path / "a.scn", "A", "x", {"zebra", "alpha"});
    writeScn(td.path / "b.scn", "B", "x", {"alpha", "monkey"});

    LinearExampleLibrary lib(td.path);
    lib.refresh();
    auto tags = lib.allTags();
    expect_eq_size(tags.size(), 3u, "3 unique tags across both entries");
    if (tags.size() >= 3) {
        expect_eq_str(tags[0], "alpha",  "tags sorted: first is alpha");
        expect_eq_str(tags[1], "monkey", "tags sorted: second is monkey");
        expect_eq_str(tags[2], "zebra",  "tags sorted: third is zebra");
    }
}

// -----------------------------------------------------------------------------
static void test_findByPath_hits_and_misses() {
    TempDir td;
    writeScn(td.path / "a.scn", "A", "x", {});
    LinearExampleLibrary lib(td.path);
    lib.refresh();

    const auto* hit = lib.findByPath(td.path / "a.scn");
    expect_true(hit != nullptr, "findByPath returns the entry for an existing file");
    if (hit) expect_eq_str(hit->title, "A", "found entry has the right title");

    const auto* miss = lib.findByPath(td.path / "nope.scn");
    expect_true(miss == nullptr, "findByPath returns nullptr for missing file");
}

// -----------------------------------------------------------------------------
static void test_refresh_picks_up_new_files() {
    TempDir td;
    writeScn(td.path / "first.scn", "First", "x", {});
    LinearExampleLibrary lib(td.path);
    lib.refresh();
    expect_eq_size(lib.entries().size(), 1u, "initial scan finds one");

    writeScn(td.path / "second.scn", "Second", "y", {});
    lib.refresh();
    expect_eq_size(lib.entries().size(), 2u, "refresh picks up the new file");
}

// -----------------------------------------------------------------------------
static void test_legacy_index_json_fills_blanks() {
    TempDir td;
    // .scn sin metadata.
    writeScn(td.path / "walkthrough_E1.scn", "", "", {});

    // index.json legacy con metadata para ese archivo.
    json idx;
    idx["version"] = "0.1";
    idx["examples"] = json::array();
    json e;
    e["id"]          = "E1";
    e["title"]       = "E1 — Lazo PD (Ogata)";
    e["file"]        = "walkthrough_E1.scn";
    e["tags"]        = json::array({"control", "ogata"});
    e["description"] = "Lazo canónico con realimentación unitaria.";
    idx["examples"].push_back(e);
    std::ofstream(td.path / "index.json") << idx.dump(2);

    LinearExampleLibrary lib(td.path);
    lib.refresh();
    expect_eq_size(lib.entries().size(), 1u, "still one entry");
    if (!lib.entries().empty()) {
        const auto& en = lib.entries()[0];
        expect_eq_str(en.title, "E1 — Lazo PD (Ogata)",
                      "title filled from legacy index.json");
        expect_eq_str(en.description, "Lazo canónico con realimentación unitaria.",
                      "description filled from legacy index.json");
        expect_eq_size(en.tags.size(), 2u, "tags filled from legacy index.json");
        expect_eq_str(en.id, "E1",
                      "legacy id 'E1' is preferred over filename stem");
    }
}

// -----------------------------------------------------------------------------
static void test_embedded_metadata_wins_over_legacy() {
    // Si el .scn ya trae metadata embebida, NO debe sobreescribirse con
    // la del index.json — la fuente de verdad es el archivo, el index
    // es sólo puente para entries no migradas.
    TempDir td;
    writeScn(td.path / "x.scn", "Embedded Title", "embedded desc", {"embedded"});

    json idx;
    idx["examples"] = json::array();
    json e;
    e["file"]        = "x.scn";
    e["title"]       = "Legacy Title";
    e["description"] = "legacy desc";
    e["tags"]        = json::array({"legacy"});
    idx["examples"].push_back(e);
    std::ofstream(td.path / "index.json") << idx.dump(2);

    LinearExampleLibrary lib(td.path);
    lib.refresh();
    expect_eq_size(lib.entries().size(), 1u, "one entry");
    if (!lib.entries().empty()) {
        const auto& en = lib.entries()[0];
        expect_eq_str(en.title, "Embedded Title",
                      "embedded title wins over legacy index.json");
        expect_eq_str(en.description, "embedded desc",
                      "embedded description wins over legacy");
        expect_eq_size(en.tags.size(), 1u, "tags unchanged");
        if (!en.tags.empty()) expect_eq_str(en.tags[0], "embedded",
                                            "tag is the embedded one");
    }
}

// -----------------------------------------------------------------------------
static void test_search_empty_text_returns_all() {
    TempDir td;
    writeScn(td.path / "a.scn", "A", "x", {});
    writeScn(td.path / "b.scn", "B", "y", {});
    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q;  // todo vacío
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 2u,
                   "empty query returns all entries (score 0)");
}

// -----------------------------------------------------------------------------
static void test_search_no_match_returns_empty() {
    TempDir td;
    writeScn(td.path / "a.scn", "Apple", "fruit", {"food"});
    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q; q.text = "tractor";
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 0u,
                   "non-matching text returns no hits");
}

// -----------------------------------------------------------------------------
static void test_matched_fields_populated() {
    TempDir td;
    writeScn(td.path / "a.scn", "PID Controller",
             "Documents the PID design.", {"pid"});
    LinearExampleLibrary lib(td.path);
    lib.refresh();

    SearchQuery q; q.text = "pid";
    auto hits = lib.search(q);
    expect_eq_size(hits.size(), 1u, "one hit");
    if (!hits.empty()) {
        const auto& mf = hits[0].matchedFields;
        bool hasTitle = false, hasTag = false, hasDesc = false;
        for (const auto& s : mf) {
            if (s == "title")        hasTitle = true;
            if (s.rfind("tag:", 0)==0) hasTag  = true;
            if (s == "description")  hasDesc  = true;
        }
        expect_true(hasTitle, "matchedFields includes 'title'");
        expect_true(hasTag,   "matchedFields includes a tag entry");
        expect_true(hasDesc,  "matchedFields includes 'description'");
    }
}

// -----------------------------------------------------------------------------
int main() {
    test_discovers_scn_files_only();
    test_empty_title_falls_back_to_stem();
    test_search_matches_title();
    test_search_matches_description_with_snippet();
    test_ranking_title_above_description();
    test_ranking_tag_exact_above_substring();
    test_must_tags_AND();
    test_any_tags_OR();
    test_all_tags_unique_sorted();
    test_findByPath_hits_and_misses();
    test_refresh_picks_up_new_files();
    test_legacy_index_json_fills_blanks();
    test_embedded_metadata_wins_over_legacy();
    test_search_empty_text_returns_all();
    test_search_no_match_returns_empty();
    test_matched_fields_populated();

    std::printf("test_example_library: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
