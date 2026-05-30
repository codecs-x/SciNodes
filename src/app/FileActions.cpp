#include "FileActions.hpp"
#include "AssetService.hpp"
#include "../core/CsvExport.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/I18n.hpp"
#include "../core/NodeInstance.hpp"
#include "../core/NodeType.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace scinodes::app {

namespace {

// Colecta los sinks del top-level graph + sus ring buffers desde el
// bridge.  Sin diferenciar por categoría sino por contar inputPorts > 0
// y outputPorts == 0 (definición operacional de "sumidero").
std::vector<scinodes::SinkExport>
collectSinkExports(const NodeGraph& g, const ScilabBridge& bridge) {
    std::vector<scinodes::SinkExport> out;
    for (const NodeInstance& n : g.nodes()) {
        const NodeDef& def = defOf(n);
        if (def.outputPorts != 0 || def.inputPorts == 0) continue;
        scinodes::SinkExport s;
        s.nodeId = n.id;
        s.label  = def.label;
        const int chCount = bridge.channelCount(n.id);
        for (int c = 0; c < chCount; ++c) {
            scinodes::SinkChannel ch;
            ch.buf         = bridge.buffer(n.id, c);
            ch.writeIndex  = bridge.writeIndex(n.id, c);
            // Si el sink tiene un portLabel custom (Oscilloscope), úsalo;
            // si no, sink_<id>_ch<c>.
            char keyL[32];
            std::snprintf(keyL, sizeof(keyL), "portLabel%d", c);
            auto it = n.stringParams.find(keyL);
            if (it != n.stringParams.end() && !it->second.empty()) {
                ch.columnHeader = it->second;
            } else {
                ch.columnHeader = def.label + "_" +
                                  std::to_string(n.id) + "_ch" +
                                  std::to_string(c);
            }
            s.channels.push_back(std::move(ch));
        }
        if (!s.channels.empty()) out.push_back(std::move(s));
    }
    return out;
}

}  // namespace

// ===========================================================================
// Menu entry points
// ===========================================================================
void FileActions::requestNew() {
    if (hasUnsavedChanges()) {
        m_pendingDestructive = PendingDestructive::New;
        m_showUnsavedModal   = true;
        return;
    }
    doNew();
}

void FileActions::requestOpen() {
    if (hasUnsavedChanges()) {
        m_pendingDestructive = PendingDestructive::OpenDialog;
        m_showUnsavedModal   = true;
        return;
    }
    doRequestOpen();
}

void FileActions::requestImport() {
    // Importar nunca destruye trabajo — no necesita gate.  Abre el
    // diálogo de archivo directamente.
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ImportLoad;
    m_fileDialog.open(FileDialog::Mode::Open,
                      scinodes::tr("dialog.import_graph"),
                      {"SciNodes graph (*.scn)", "*.scn"});
}

void FileActions::requestImportModel3D() {
    // Importar un modelo 3D al catálogo del proyecto.  No destruye trabajo:
    // sólo añade una entrada al `importedObjects()` del NodeGraph y al
    // cache by-name del AssetService.  Los nodos Object3D que lo
    // referencien por nombre lo resuelven a partir del siguiente frame.
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ImportModel3D;
    m_fileDialog.open(FileDialog::Mode::Open,
                      scinodes::tr("dialog.import_model_3d"),
                      {"glTF model (*.gltf;*.glb)", "*.gltf;*.glb"});
}

void FileActions::openFromPath(const std::string& path) {
    if (hasUnsavedChanges()) {
        m_pendingDestructive   = PendingDestructive::LoadPath;
        m_pendingLoadPath      = path;
        m_pendingLoadIsExample = false;
        m_showUnsavedModal     = true;
        return;
    }
    doLoad(path);
}

void FileActions::openExample(const std::string& path) {
    if (hasUnsavedChanges()) {
        m_pendingDestructive   = PendingDestructive::LoadPath;
        m_pendingLoadPath      = path;
        m_pendingLoadIsExample = true;
        m_showUnsavedModal     = true;
        return;
    }
    doLoad(path);
    m_currentIsExample = true;
}

void FileActions::importFromPath(const std::string& path) {
    doImport(path);
}

void FileActions::requestQuit() {
    if (hasUnsavedChanges()) {
        m_pendingDestructive = PendingDestructive::Quit;
        m_showUnsavedModal   = true;
        return;
    }
    m_quitGranted = true;
}

bool FileActions::hasUnsavedChanges() const {
    return m_canvas.dirtyRevision() != m_savedRev;
}

void FileActions::markSaved() {
    m_savedRev = m_canvas.dirtyRevision();
}

void FileActions::doNew() {
    m_canvas.clear();
    m_sim.stop();
    m_currentPath.clear();
    m_currentIsExample = false;
    markSaved();
}

void FileActions::doRequestOpen() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::OpenLoad;
    m_fileDialog.open(FileDialog::Mode::Open,
                      scinodes::tr("dialog.open_graph"),
                      {"SciNodes graph (*.scn)", "*.scn"});
}

void FileActions::requestSave() {
    // Si el grafo viene de la biblioteca de ejemplos, Save se redirige
    // a Save As para no sobreescribir el template original.  Un usuario
    // que parte de un ejemplo casi siempre quiere conservar la copia
    // canónica para reabrirla limpia.
    if (m_currentPath.empty() || m_currentIsExample) { requestSaveAs(); return; }
    doSave(m_currentPath);
}

void FileActions::requestSaveAsExample() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::SaveAs;
    // Sugerir la carpeta de ejemplos como punto de partida.  El usuario
    // puede salirse de ahí si quiere, pero el default invita a guardar
    // en el lugar correcto para que el browser lo recoja al reabrir.
    const std::string suggested = "examples/graphs/my_example.scn";
    m_fileDialog.open(FileDialog::Mode::Save,
                      scinodes::trOr("dialog.save_as_example",
                                     "Save graph as example"),
                      {"SciNodes graph (*.scn)", "*.scn"},
                      suggested);
}

void FileActions::requestSaveAs() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::SaveAs;
    std::string suggested = m_currentPath.empty() ? "graph.scn" : m_currentPath;
    m_fileDialog.open(FileDialog::Mode::Save,
                      scinodes::tr("dialog.save_graph"),
                      {"SciNodes graph (*.scn)", "*.scn"},
                      suggested);
}

void FileActions::requestExportSod() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ExportSod;
    m_fileDialog.open(FileDialog::Mode::Save,
                      scinodes::tr("dialog.export.sod"),
                      {"Scilab data (*.sod)", "*.sod"},
                      "simulation.sod");
}

void FileActions::requestExportCsvWide() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ExportCsvWide;
    m_fileDialog.open(FileDialog::Mode::Save,
                      scinodes::tr("dialog.export.csv_wide"),
                      {"CSV (*.csv)", "*.csv"},
                      "simulation.csv");
}

void FileActions::requestExportCsvFolder() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ExportCsvFolder;
    // FileDialog::Mode::Save con un sufijo "/" sugerido — el usuario
    // elige el nombre de la carpeta destino.  Nuestro FileDialog no
    // tiene Mode::Directory; el handler de pollFileDialog elimina la
    // extensión y trata el path como directorio.
    m_fileDialog.open(FileDialog::Mode::Save,
                      scinodes::tr("dialog.export.csv_folder"),
                      {"Folder", "*"},
                      "simulation_csv");
}

// ===========================================================================
// CLI bootstrap
// ===========================================================================
void FileActions::openFromCli(const std::string& path) {
    LoadReport r = m_canvas.loadFromFile(path);
    if (!r.ok) {
        std::fprintf(stderr,
            "[SciNodes] No pude abrir %s: %s\n",
            path.c_str(),
            r.fatalError.empty() ? "archivo no válido"
                                 : r.fatalError.c_str());
        return;
    }
    m_lastReport  = r;
    m_currentPath = path;
    markSaved();
    std::fprintf(stderr,
        "[SciNodes] Grafo cargado: %s (%d nodos, %d aristas)\n",
        path.c_str(), r.nodesLoaded, r.edgesLoaded);
}

// ===========================================================================
// Per-frame update — drain dialog + render popups
// ===========================================================================
void FileActions::update() {
    pollFileDialog();
    renderLoadReportPopup();
    renderLoadErrorPopup();
    renderUnsavedChangesPopup();
}

void FileActions::pollFileDialog() {
    if (m_fileDialog.isOpen()) return;
    std::string picked = m_fileDialog.take();
    if (picked.empty()) return;

    PendingAction act = m_pendingAction;
    m_pendingAction = PendingAction::None;

    auto appendIfMissing = [&](const char* ext) {
        auto dot = picked.rfind('.');
        auto sep = picked.find_last_of("/\\");
        if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
            picked += ext;
    };

    if (act == PendingAction::OpenLoad) {
        doLoad(picked);
    } else if (act == PendingAction::ImportLoad) {
        doImport(picked);
    } else if (act == PendingAction::ImportModel3D) {
        doImportModel3D(picked);
    } else if (act == PendingAction::SaveAs) {
        appendIfMissing(".scn");
        doSave(picked);
    } else if (act == PendingAction::ExportSod) {
        appendIfMissing(".sod");
        doExportSod(picked);
    } else if (act == PendingAction::ExportCsvWide) {
        appendIfMissing(".csv");
        doExportCsvWide(picked);
    } else if (act == PendingAction::ExportCsvFolder) {
        doExportCsvFolder(picked);
    }
}

void FileActions::doLoad(const std::string& path) {
    LoadReport r = m_canvas.loadFromFile(path);
    m_lastReport = r;
    m_sim.stop();
    if (!r.ok) {
        m_showErrorPopup  = true;
        m_currentPath.clear();
        m_currentIsExample = false;
        return;
    }
    m_currentPath      = path;
    m_currentIsExample = false;   // doLoad regular; openExample() lo set después
    if (r.hasViolations()) m_showReportPopup = true;
    // El grafo recién cargado define el nuevo baseline de "guardado".
    markSaved();
}

void FileActions::doImportModel3D(const std::string& path) {
    // 1) Parsear .gltf/.glb contract-less (todos los meshes como parts).
    std::string err;
    auto asset = scinodes::DeviceAssetLoader::loadCatalog(path, &err);
    if (asset.parts.empty()) {
        m_lastReport.fatalError = err.empty()
            ? ("No se encontraron meshes en " + path)
            : err;
        m_showErrorPopup = true;
        return;
    }
    // 2) Nombre del catálogo = stem del archivo (sin extensión ni dir).
    auto sep = path.find_last_of("/\\");
    std::string stem = (sep == std::string::npos) ? path : path.substr(sep + 1);
    auto dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    if (stem.empty()) stem = "imported";

    // 3) Catálogo del proyecto: ruta + lista de partes (las claves de
    //    asset.parts).  Los Object3D referencian "<stem>/<partName>".
    ImportedObject obj;
    obj.name = stem;
    obj.path = path;
    for (const auto& [partName, _mesh] : asset.parts)
        obj.parts.push_back(partName);
    m_canvas.addImportedObject(std::move(obj));

    // 4) Cache by-name del AssetService — el resolver del SceneCollector
    //    lo consulta para cada Object3D.objectRef.
    if (auto* svc = m_canvas.assetService()) {
        svc->installNamedAsset(stem, std::move(asset));
    }
}

void FileActions::doImport(const std::string& path) {
    // Implementación real en task (#288); por ahora stub.  Mantenemos
    // el método aquí declarado para no romper el control flow del
    // pollFileDialog mientras la feature se construye en otra rama.
    auto res = m_canvas.importFromFile(path);
    if (!res.ok) {
        m_lastReport.fatalError = res.error;
        m_showErrorPopup        = true;
        return;
    }
    if (!res.report.rejectedEdges.empty() || !res.report.unknownTypes.empty()) {
        m_lastReport     = res.report;
        m_currentPath    = path;  // sólo para el popup; restaurado abajo
        m_showReportPopup = true;
    }
}

void FileActions::doSave(const std::string& path) {
    if (!m_canvas.saveToFile(path)) return;
    // Falla silenciosa: el único caso realista es permisos de FS, y el
    // diálogo nativo ya gatea contra eso.
    m_currentPath      = path;
    m_currentIsExample = false;   // Save-As/As-Example sale del modo template
    markSaved();
    // Si el save se hizo como parte de un flujo "guardar y luego X",
    // disparar la acción destructiva pendiente.
    if (m_runPendingAfterSave) {
        m_runPendingAfterSave = false;
        runPendingDestructive();
    }
}

void FileActions::runPendingDestructive() {
    PendingDestructive d = m_pendingDestructive;
    m_pendingDestructive  = PendingDestructive::None;
    switch (d) {
        case PendingDestructive::None: break;
        case PendingDestructive::New:           doNew(); break;
        case PendingDestructive::OpenDialog:    doRequestOpen(); break;
        case PendingDestructive::LoadPath: {
            const std::string p = std::move(m_pendingLoadPath);
            const bool isExample = m_pendingLoadIsExample;
            m_pendingLoadPath.clear();
            m_pendingLoadIsExample = false;
            doLoad(p);
            if (isExample) m_currentIsExample = true;
            break;
        }
        case PendingDestructive::Quit:          m_quitGranted = true; break;
    }
}

void FileActions::doExportSod(const std::string& path) {
    bool accepted = m_bridge.exportSod(path);
    if (!accepted) {
        std::string r = m_bridge.takeLastExportResult();
        m_exportStatus = r.empty() ? scinodes::tr("toast.export.sod_no_session")
                                   : r;
        m_exportStatusTimer = 5.0f;
    }
    // Exports en cola completan async dentro del solver thread; su
    // resultado llega vía takeLastExportResult() en drawExportToast.
}

void FileActions::doExportCsvWide(const std::string& path) {
    auto sinks = collectSinkExports(m_canvas.graph(), m_bridge);
    if (sinks.empty()) {
        m_exportStatus      = scinodes::tr("toast.export.no_sinks");
        m_exportStatusTimer = 5.0f;
        return;
    }
    std::string err;
    const bool ok = scinodes::writeAllSinksWide(
        path, sinks, m_bridge.time(), m_bridge.solverDt(), &err);
    m_exportStatus = ok
        ? (scinodes::tr("toast.export.csv_ok_prefix") + ": " + path)
        : (scinodes::tr("toast.export.csv_failed_prefix") + ": " + err);
    m_exportStatusTimer = 5.0f;
}

void FileActions::doExportCsvFolder(const std::string& path) {
    auto sinks = collectSinkExports(m_canvas.graph(), m_bridge);
    if (sinks.empty()) {
        m_exportStatus      = scinodes::tr("toast.export.no_sinks");
        m_exportStatusTimer = 5.0f;
        return;
    }
    std::string err;
    const bool ok = scinodes::writeAllSinksFolder(
        path, sinks, m_bridge.time(), m_bridge.solverDt(), &err);
    m_exportStatus = ok
        ? (scinodes::tr("toast.export.csv_folder_ok_prefix") + ": " + path)
        : (scinodes::tr("toast.export.csv_folder_failed_prefix") + ": " + err);
    m_exportStatusTimer = 5.0f;
}

// ===========================================================================
// Toast del .sod export
// ===========================================================================
void FileActions::drawExportToast() {
    // Drena un resultado nuevo del bridge.
    if (std::string r = m_bridge.takeLastExportResult(); !r.empty()) {
        m_exportStatus      = std::move(r);
        m_exportStatusTimer = 5.0f;
    }

    if (m_exportStatusTimer <= 0.0f) return;

    float dt = ImGui::GetIO().DeltaTime;
    m_exportStatusTimer -= dt;
    float alpha = std::min(1.0f, m_exportStatusTimer / 1.0f);
    bool  isError =
        m_exportStatus.find("failed")  != std::string::npos ||
        m_exportStatus.find("refused") != std::string::npos;

    ImU32 col = isError
        ? IM_COL32(230, 110,  90, static_cast<int>(255 * alpha))
        : IM_COL32(140, 220, 140, static_cast<int>(255 * alpha));
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted("   ");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_exportStatus.c_str());
    ImGui::PopStyleColor();
}

// ===========================================================================
// Modales LoadReport / LoadError
// ===========================================================================
void FileActions::renderLoadReportPopup() {
    if (m_showReportPopup) {
        ImGui::OpenPopup("##LoadReport");
        m_showReportPopup = false;
    }

    ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##LoadReport", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 170, 60, 255));
        ImGui::TextUnformatted(" Loaded with grammar violations");
        ImGui::PopStyleColor();
        ImGui::Separator();

        ImGui::Text("File:  %s", m_currentPath.c_str());
        ImGui::Text("Nodes loaded:  %d", m_lastReport.nodesLoaded);
        ImGui::Text("Edges loaded:  %d", m_lastReport.edgesLoaded);
        ImGui::Text("Edges dropped: %d", (int)m_lastReport.rejectedEdges.size());

        if (!m_lastReport.unknownTypes.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Unknown node types or notes:");
            for (const auto& s : m_lastReport.unknownTypes)
                ImGui::BulletText("%s", s.c_str());
        }

        if (!m_lastReport.rejectedEdges.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Rejected edges:");
            for (const auto& e : m_lastReport.rejectedEdges)
                ImGui::BulletText("[%s] node %d → node %d   %s",
                                  e.rule.c_str(),
                                  e.fromNodeId, e.toNodeId,
                                  e.message.c_str());
        }

        ImGui::Spacing();
        ImGui::TextDisabled("The graph is open in read-only mode.\n"
                            "Use File → New to start a fresh canvas.");
        ImGui::Separator();
        if (ImGui::Button("OK", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void FileActions::renderUnsavedChangesPopup() {
    // ID estable (independiente del locale) compartido entre OpenPopup
    // y BeginPopupModal — sin esto ImGui no reconoce el match y el
    // modal nunca aparece.  El título visible va dentro del popup.
    if (m_showUnsavedModal) {
        ImGui::OpenPopup("##UnsavedChanges");
        m_showUnsavedModal = false;
    }

    ImGui::SetNextWindowSize({440, 0}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##UnsavedChanges", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 170, 60, 255));
    ImGui::TextUnformatted(
        scinodes::trOr("dialog.unsaved.title", "Unsaved changes").c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::TextWrapped("%s",
        scinodes::trOr("dialog.unsaved.body",
            "The current graph has unsaved changes. "
            "Continuing will discard your work.").c_str());
    ImGui::Separator();

    if (ImGui::Button(scinodes::trOr("dialog.unsaved.save", "Save").c_str(),
                      {120, 0})) {
        // Encadenamos save → acción destructiva pendiente.  Si no hay
        // currentPath, requestSave delega a SaveAs y el flujo asíncrono
        // del file dialog completa el ciclo desde pollFileDialog/doSave.
        m_runPendingAfterSave = true;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        requestSave();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(scinodes::trOr("dialog.unsaved.discard", "Discard").c_str(),
                      {120, 0})) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        runPendingDestructive();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(scinodes::trOr("dialog.unsaved.cancel", "Cancel").c_str(),
                      {120, 0})) {
        m_pendingDestructive  = PendingDestructive::None;
        m_pendingLoadPath.clear();
        m_runPendingAfterSave = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileActions::renderLoadErrorPopup() {
    if (m_showErrorPopup) {
        ImGui::OpenPopup("##LoadError");
        m_showErrorPopup = false;
    }

    ImGui::SetNextWindowSize({480, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##LoadError", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 90, 90, 255));
        ImGui::TextUnformatted(" Could not load file");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_lastReport.fatalError.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace scinodes::app
