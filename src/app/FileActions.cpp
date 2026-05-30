#include "FileActions.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace scinodes::app {

// ===========================================================================
// Menu entry points
// ===========================================================================
void FileActions::requestNew() {
    m_canvas.clear();
    m_sim.stop();
    m_currentPath.clear();
}

void FileActions::requestOpen() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::OpenLoad;
    m_fileDialog.open(FileDialog::Mode::Open,
                      "Open SciNodes Graph",
                      {"SciNodes graph (*.scn)", "*.scn"});
}

void FileActions::requestSave() {
    if (m_currentPath.empty()) { requestSaveAs(); return; }
    doSave(m_currentPath);
}

void FileActions::requestSaveAs() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::SaveAs;
    std::string suggested = m_currentPath.empty() ? "graph.scn" : m_currentPath;
    m_fileDialog.open(FileDialog::Mode::Save,
                      "Save SciNodes Graph",
                      {"SciNodes graph (*.scn)", "*.scn"},
                      suggested);
}

void FileActions::requestExportSod() {
    if (m_fileDialog.isOpen()) return;
    m_pendingAction = PendingAction::ExportSod;
    m_fileDialog.open(FileDialog::Mode::Save,
                      "Export Simulation Data (SOD)",
                      {"Scilab data (*.sod)", "*.sod"},
                      "simulation.sod");
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
    } else if (act == PendingAction::SaveAs) {
        appendIfMissing(".scn");
        doSave(picked);
    } else if (act == PendingAction::ExportSod) {
        appendIfMissing(".sod");
        doExportSod(picked);
    }
}

void FileActions::doLoad(const std::string& path) {
    LoadReport r = m_canvas.loadFromFile(path);
    m_lastReport = r;
    m_sim.stop();
    if (!r.ok) {
        m_showErrorPopup  = true;
        m_currentPath.clear();
    } else {
        m_currentPath = path;
        if (r.hasViolations()) m_showReportPopup = true;
    }
}

void FileActions::doSave(const std::string& path) {
    if (m_canvas.saveToFile(path))
        m_currentPath = path;
    // Falla silenciosa: el único caso realista es permisos de FS, y el
    // diálogo nativo ya gatea contra eso.
}

void FileActions::doExportSod(const std::string& path) {
    bool accepted = m_bridge.exportSod(path);
    if (!accepted) {
        std::string r = m_bridge.takeLastExportResult();
        m_exportStatus = r.empty() ? "SOD export refused (no Scilab session)."
                                   : r;
        m_exportStatusTimer = 5.0f;
    }
    // Exports en cola completan async dentro del solver thread; su
    // resultado llega vía takeLastExportResult() en drawExportToast.
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
