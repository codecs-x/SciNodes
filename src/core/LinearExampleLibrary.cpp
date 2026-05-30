#include "LinearExampleLibrary.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;
using nlohmann::json;

namespace scinodes {

namespace {

// Pesos del ranking — explícitos para no esconder números mágicos.
// La intuición: un match en el título pesa más que en la descripción,
// que pesa más que en el id derivado del filename.  Los tags pesan
// mucho cuando matchean por completo (palabra exacta) y un poco
// cuando matchean como substring (la mitad del peso descripción).
constexpr float kScoreTitle           = 4.0f;
constexpr float kScoreTagExact        = 3.0f;
constexpr float kScoreDescription     = 2.0f;
constexpr float kScoreTagSubstring    = 1.5f;
constexpr float kScoreId              = 1.0f;

// Cuántos caracteres de contexto rodean el match en el snippet.
constexpr std::size_t kSnippetContext = 40;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Parte una query string libre en tokens minúsculos, separados por
// whitespace.  Tokens vacíos descartados.  Sin escaping ni quoting —
// "foo bar" busca por separado "foo" y "bar".
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        std::string lower = toLower(tok);
        if (!lower.empty()) out.push_back(std::move(lower));
    }
    return out;
}

// Genera un snippet con `±kSnippetContext` caracteres alrededor del
// primer match de `tokenLower` dentro de `descLower`/`descOrig`.  Marca
// el match con `**...**`.  Devuelve cadena vacía si no hubo match.
std::string makeSnippet(const std::string& descOrig,
                        const std::string& descLower,
                        const std::string& tokenLower) {
    if (tokenLower.empty() || descLower.empty()) return {};
    auto pos = descLower.find(tokenLower);
    if (pos == std::string::npos) return {};

    const std::size_t start = pos > kSnippetContext ? pos - kSnippetContext : 0;
    const std::size_t end   = std::min(descOrig.size(),
                                       pos + tokenLower.size() + kSnippetContext);

    std::string snippet;
    if (start > 0) snippet += "…";
    snippet += descOrig.substr(start, pos - start);
    snippet += "**";
    snippet += descOrig.substr(pos, tokenLower.size());
    snippet += "**";
    snippet += descOrig.substr(pos + tokenLower.size(), end - (pos + tokenLower.size()));
    if (end < descOrig.size()) snippet += "…";
    return snippet;
}

// Lee la metadata de un archivo `.scn` directamente del JSON, sin pasar
// por ScnSerializer::deserialize (que reconstruiría todo el grafo).
// Para la biblioteca sólo necesitamos title/description/tags.  Si el
// parseo falla devolvemos nullopt — la entry se omite con un cargo de
// log silencioso (la UI puede ofrecer "reintentar" o un fallback).
struct RawHeader {
    std::string              id;
    std::string              title;
    std::string              description;
    std::vector<std::string> tags;
};
std::optional<RawHeader> readScnHeader(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    std::stringstream ss; ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str()); }
    catch (...) { return std::nullopt; }
    if (!j.is_object()) return std::nullopt;

    RawHeader h;
    if (j.contains("id")          && j["id"].is_string())          h.id          = j["id"].get<std::string>();
    if (j.contains("title")       && j["title"].is_string())       h.title       = j["title"].get<std::string>();
    if (j.contains("description") && j["description"].is_string()) h.description = j["description"].get<std::string>();
    if (j.contains("tags") && j["tags"].is_array())
        for (const auto& t : j["tags"])
            if (t.is_string()) h.tags.push_back(t.get<std::string>());
    return h;
}

// Lee el manifest legacy `index.json`.  Devuelve un map { filename → entry }
// para que el caller pueda fusionar con las entries que ya leyó del disco.
struct LegacyMeta {
    std::string              id;
    std::string              title;
    std::string              description;
    std::vector<std::string> tags;
};
std::unordered_map<std::string, LegacyMeta> readLegacyIndex(const fs::path& indexFile) {
    std::unordered_map<std::string, LegacyMeta> out;
    std::ifstream in(indexFile);
    if (!in) return out;
    std::stringstream ss; ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str()); }
    catch (...) { return out; }
    if (!j.is_object() || !j.contains("examples") || !j["examples"].is_array())
        return out;
    for (const auto& je : j["examples"]) {
        if (!je.is_object()) continue;
        LegacyMeta m;
        if (je.contains("id")          && je["id"].is_string())          m.id          = je["id"].get<std::string>();
        if (je.contains("title")       && je["title"].is_string())       m.title       = je["title"].get<std::string>();
        if (je.contains("description") && je["description"].is_string()) m.description = je["description"].get<std::string>();
        if (je.contains("tags") && je["tags"].is_array())
            for (const auto& t : je["tags"])
                if (t.is_string()) m.tags.push_back(t.get<std::string>());
        std::string file;
        if (je.contains("file") && je["file"].is_string()) file = je["file"].get<std::string>();
        if (!file.empty()) out.emplace(file, std::move(m));
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
LinearExampleLibrary::LinearExampleLibrary(fs::path directory)
    : m_directory(std::move(directory)) {}

void LinearExampleLibrary::refresh() {
    m_entries.clear();
    if (!fs::exists(m_directory) || !fs::is_directory(m_directory)) return;

    // 1. Escanear *.scn al nivel raíz (sin recursión por ahora).
    for (const auto& de : fs::directory_iterator(m_directory)) {
        if (!de.is_regular_file()) continue;
        if (de.path().extension() != ".scn") continue;

        auto header = readScnHeader(de.path());
        if (!header) continue;  // archivo ilegible — saltarlo silenciosamente

        ExampleEntry e;
        e.file        = fs::absolute(de.path());
        // Prioridad del id: campo embebido > stem del filename.  Eso
        // permite que un `.scn` siga siendo "E1" aunque el archivo
        // se llame `walkthrough_E1.scn` o se renombre.
        e.id          = !header->id.empty()
                            ? header->id
                            : de.path().stem().string();
        e.title       = header->title;
        e.description = header->description;
        e.tags        = std::move(header->tags);
        m_entries.push_back(std::move(e));
    }

    // 2. Puente con index.json legacy: si una entry no trae metadata
    //    embebida (formato 0.4 sin migrar todavía), completar desde
    //    el manifest sidecar.  Cuando todos los .scn estén migrados
    //    el index.json se borra y esta rama queda dead-code para
    //    limpiar.  No fuerza un orden — sólo enriquece.
    const fs::path legacyPath = m_directory / "index.json";
    if (fs::exists(legacyPath)) {
        auto legacy = readLegacyIndex(legacyPath);
        for (auto& e : m_entries) {
            auto it = legacy.find(e.file.filename().string());
            if (it == legacy.end()) continue;
            const auto& l = it->second;
            if (e.title.empty()       && !l.title.empty())       e.title       = l.title;
            if (e.description.empty() && !l.description.empty()) e.description = l.description;
            if (e.tags.empty()        && !l.tags.empty())        e.tags        = l.tags;
            // Si el id legacy es más expresivo (ej. "E1" vs "walkthrough_E1")
            // lo preferimos sobre el stem del filename.
            if (!l.id.empty() && l.id.size() < e.id.size()) e.id = l.id;
        }
    }

    // 3. Fallback final: si el título sigue vacío, usar el stem.  Eso
    //    garantiza que la UI nunca muestre una entry sin nombre.
    for (auto& e : m_entries)
        if (e.title.empty()) e.title = e.file.stem().string();
}

std::vector<std::string> LinearExampleLibrary::allTags() const {
    std::set<std::string> uniq;
    for (const auto& e : m_entries)
        for (const auto& t : e.tags) uniq.insert(t);
    return std::vector<std::string>(uniq.begin(), uniq.end());
}

std::vector<SearchHit>
LinearExampleLibrary::search(const SearchQuery& q) const {
    std::vector<SearchHit> hits;
    const std::vector<std::string> tokens = tokenize(q.text);

    // Lower-case del must/any para comparación case-insensitive.
    auto lowerVec = [](const std::vector<std::string>& v) {
        std::vector<std::string> out; out.reserve(v.size());
        for (const auto& s : v) out.push_back(toLower(s));
        return out;
    };
    const auto mustLower = lowerVec(q.mustTags);
    const auto anyLower  = lowerVec(q.anyTags);

    for (const auto& e : m_entries) {
        // Pre-cómputo de tags lower para AND/OR.
        std::vector<std::string> tagsLower; tagsLower.reserve(e.tags.size());
        for (const auto& t : e.tags) tagsLower.push_back(toLower(t));

        // AND: todos los mustTags deben estar entre los tags de e.
        bool passMust = true;
        for (const auto& mt : mustLower) {
            if (std::find(tagsLower.begin(), tagsLower.end(), mt) == tagsLower.end()) {
                passMust = false; break;
            }
        }
        if (!passMust) continue;

        // OR: al menos un anyTag debe estar entre los tags de e (si no
        // se especificaron anyTags, esto pasa trivialmente).
        if (!anyLower.empty()) {
            bool passAny = false;
            for (const auto& at : anyLower) {
                if (std::find(tagsLower.begin(), tagsLower.end(), at) != tagsLower.end()) {
                    passAny = true; break;
                }
            }
            if (!passAny) continue;
        }

        // Scoring textual.  Sin tokens: la entry pasa con score 0 (sólo
        // aporta el filtrado de tags).
        SearchHit hit;
        hit.entry = e;
        hit.score = 0.0f;

        if (!tokens.empty()) {
            const std::string titleLower = toLower(e.title);
            const std::string descLower  = toLower(e.description);
            const std::string idLower    = toLower(e.id);

            std::set<std::string> matched;  // dedupea matchedFields
            bool anyTokenMatched = false;

            for (const auto& tok : tokens) {
                bool tokenMatchedHere = false;

                if (!titleLower.empty() && titleLower.find(tok) != std::string::npos) {
                    hit.score += kScoreTitle;
                    matched.insert("title");
                    tokenMatchedHere = true;
                }
                // tag exact: tok == tag (case-insensitive completo).
                // tag substring: tok ⊂ tag pero no igual.
                for (std::size_t i = 0; i < tagsLower.size(); ++i) {
                    if (tagsLower[i] == tok) {
                        hit.score += kScoreTagExact;
                        matched.insert("tag:" + e.tags[i]);
                        tokenMatchedHere = true;
                    } else if (tagsLower[i].find(tok) != std::string::npos) {
                        hit.score += kScoreTagSubstring;
                        matched.insert("tag:" + e.tags[i]);
                        tokenMatchedHere = true;
                    }
                }
                if (!descLower.empty() && descLower.find(tok) != std::string::npos) {
                    hit.score += kScoreDescription;
                    matched.insert("description");
                    tokenMatchedHere = true;
                    if (hit.snippet.empty())
                        hit.snippet = makeSnippet(e.description, descLower, tok);
                }
                if (!idLower.empty() && idLower.find(tok) != std::string::npos) {
                    hit.score += kScoreId;
                    matched.insert("id");
                    tokenMatchedHere = true;
                }

                if (tokenMatchedHere) anyTokenMatched = true;
            }

            // Si hay texto pero NINGÚN token matchó en ningún campo,
            // la entry no aparece — distinto a "score 0 con tags válidos".
            if (!anyTokenMatched) continue;

            hit.matchedFields.assign(matched.begin(), matched.end());
        }

        hits.push_back(std::move(hit));
    }

    std::sort(hits.begin(), hits.end(),
              [](const SearchHit& a, const SearchHit& b) {
                  if (a.score != b.score) return a.score > b.score;
                  // Tie-break determinista por título para que el orden
                  // de los hits no dependa del orden de directory_iterator.
                  return a.entry.title < b.entry.title;
              });

    if (q.limit > 0 && hits.size() > q.limit) hits.resize(q.limit);
    return hits;
}

const ExampleEntry*
LinearExampleLibrary::findByPath(const fs::path& file) const {
    std::error_code ec;
    const fs::path target = fs::weakly_canonical(file, ec);
    for (const auto& e : m_entries) {
        if (e.file == file) return &e;
        if (!ec && fs::weakly_canonical(e.file, ec) == target) return &e;
    }
    return nullptr;
}

}  // namespace scinodes
