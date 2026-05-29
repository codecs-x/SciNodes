#include "PlotPanel.hpp"
#include "../core/CsvExport.hpp"
#include "../core/Fft.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include "plots/HeatmapRenderer.hpp"
#include "plots/HistogramRenderer.hpp"
#include "plots/PhaseRenderer.hpp"
#include "plots/SpectrumRenderer.hpp"
#include "plots/WaveRenderer.hpp"


static bool isSink(NodeType t) {
    return t == NodeType::Oscilloscope  ||
           t == NodeType::FFTAnalyzer   ||
           t == NodeType::PhasePortrait ||
           t == NodeType::DataLogger    ||
           t == NodeType::TerminalDisplay ||
           t == NodeType::HeatmapSink ||
           t == NodeType::DistributionSink;
}

static bool hasIncomingEdge(int nodeId, const NodeGraph& g) {
    for (const auto& e : g.edges())
        if (e.toNodeId == nodeId) return true;
    return false;
}

// -------------------------------------------------------------------------
// Renderers movidos a src/ui/plots/<Name>Renderer.{hpp,cpp}.
// -------------------------------------------------------------------------

void PlotPanel::drawContent(const NodeGraph& graph, const ScilabBridge& bridge) {
    // ---- Drain any pending CSV save dialog --------------------------------
    if (m_pendingExportSinkId != 0 && !m_exportDialog.isOpen()) {
        std::string path = m_exportDialog.take();
        if (!path.empty()) {
            // Default suffix
            if (path.size() < 4 ||
                path.substr(path.size() - 4) != ".csv")
                path += ".csv";
            std::string err;
            float dt = bridge.solverDt();
            if (dt <= 0.0f) dt = 1.0f / 60.0f;     // fallback when never stepped
            bool ok = scinodes::writeSinkCsv(
                path,
                bridge.buffer(m_pendingExportSinkId),
                bridge.writeIndex(m_pendingExportSinkId),
                bridge.time(), dt,
                m_pendingExportLabel,
                &err);
            m_exportStatus = ok ? ("Exported to " + path)
                                : ("Export failed: " + err);
        }
        m_pendingExportSinkId = 0;
        m_pendingExportLabel.clear();
    }

    // Focus-follows-mouse estilo Blender (ver NodeCanvas para racional).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive()) {
        ImGui::SetWindowFocus();
    }

    // Envolvemos el contenido en un BeginChild scrollable para que
    // cuando haya varios sinks (Osc + Phase + FFT + ...) el usuario
    // pueda hacer scroll vertical en el panel.
    ImGui::BeginChild("##PlotsScroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar |
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Only show plots for sink nodes that have at least one incoming edge
    std::vector<const NodeInstance*> sinks;
    for (const auto& n : graph.nodes())
        if (isSink(n.type) && hasIncomingEdge(n.id, graph))
            sinks.push_back(&n);

    if (sinks.empty()) {
        // No plotter nodes — show a hint instead of a spurious waveform
        float avail = ImGui::GetContentRegionAvail().y;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail * 0.4f);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 200));
        const char* msg = "Connect a signal to an Oscilloscope,\nFFT Analyzer or Data Logger to see plots.";
        float tw = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
        ImGui::TextUnformatted(msg);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    // Divide available height among sink plots
    int   totalPlots = static_cast<int>(sinks.size());
    float avail      = ImGui::GetContentRegionAvail().y;
    float plotH      = std::max(55.f, (avail - totalPlots * 32.f) /
                                      static_cast<float>(totalPlots));
    float plotW      = ImGui::GetContentRegionAvail().x;

    // ---- Per-sink plots ----------------------------------------------------
    //
    // Cada sink se renderiza dentro de un CollapsingHeader (estilo
    // Blender Outliner): colapsado muestra solo el nombre, expandido
    // muestra la gráfica.  Permite manejar muchas plots en simultáneo
    // sin saturar el panel.  El estado abierto/cerrado lo recuerda
    // ImGui por sí mismo a partir del id del nodo.
    for (const NodeInstance* n : sinks) {
        char label[64];
        std::snprintf(label, sizeof(label), "%s  #%d", labelOf(n->type), n->id);

        ImGui::PushID(n->id);
        ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(43, 80, 140, 200));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(55,100, 170, 220));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(70,120, 200, 230));
        const bool open = ImGui::CollapsingHeader(
            label, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(3);

        if (!open) {
            // Indicación visual del valor actual aun cuando esté
            // contraído — útil para escanear muchos sinks de un
            // vistazo sin abrir cada uno.
            const auto& buf = bridge.buffer(n->id);
            if (!buf.empty()) {
                int w = bridge.writeIndex(n->id);
                const int N = (int)buf.size();
                float lastVal = buf[(w - 1 + N) % N];
                ImGui::SameLine();
                ImGui::TextDisabled("  →  %.4g", lastVal);
            }
            ImGui::Spacing();
            ImGui::PopID();
            continue;
        }

        if (n->type == NodeType::FFTAnalyzer) {
            auto it = n->params.find("Bin Count");
            int binCount = (it != n->params.end()) ? (int)it->second : 256;
            scinodes::ui::plots::renderSpectrum("##fft",
                           bridge.buffer(n->id), bridge.writeIndex(n->id),
                           binCount, plotW, plotH,
                           IM_COL32(230, 160, 100, 255));   // orange
        } else if (n->type == NodeType::PhasePortrait) {
            scinodes::ui::plots::renderPhase(bridge.buffer(n->id, 0), bridge.writeIndex(n->id, 0),
                        bridge.buffer(n->id, 1), bridge.writeIndex(n->id, 1),
                        plotW, plotH,
                        IM_COL32(180, 230, 130, 255),         // green
                        m_zoomStates[n->id]);
        } else if (n->type == NodeType::HeatmapSink) {
            scinodes::ui::plots::renderHeatmap(bridge.buffer(n->id, 0), bridge.writeIndex(n->id, 0),
                          bridge.buffer(n->id, 1), bridge.writeIndex(n->id, 1),
                          bridge.buffer(n->id, 2), bridge.writeIndex(n->id, 2),
                          plotW, plotH);
        } else if (n->type == NodeType::DistributionSink) {
            auto it = n->params.find("Bin Count");
            int bins = (it != n->params.end()) ? (int)it->second : 20;
            scinodes::ui::plots::renderHistogram(bridge.buffer(n->id), bridge.writeIndex(n->id),
                            bins, plotW, plotH,
                            IM_COL32(220, 110, 170, 220));   // structural-pink
        } else {
            // ZoomState per-nodo: la primera llamada inserta una entrada
            // con manual=false y los siguientes frames la reutilizan.
            scinodes::ui::plots::renderWave("##sink",
                       bridge.buffer(n->id), bridge.writeIndex(n->id),
                       plotW, plotH,
                       IM_COL32(100, 160, 230, 255),       // blue
                       m_zoomStates[n->id],
                       bridge.solverDt() > 0 ? bridge.solverDt() : 1.0f/60.0f,
                       bridge.time());
        }

        // ---- Export button for DataLogger sinks --------------------------
        if (n->type == NodeType::DataLogger) {
            bool busy = m_exportDialog.isOpen() || m_pendingExportSinkId != 0;
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton("Export CSV…")) {
                m_pendingExportSinkId = n->id;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "%s #%d",
                              labelOf(n->type), n->id);
                m_pendingExportLabel = lbl;
                char suggested[64];
                std::snprintf(suggested, sizeof(suggested),
                              "scinodes_logger_%d.csv", n->id);
                m_exportDialog.open(FileDialog::Mode::Save,
                                    "Export DataLogger to CSV",
                                    { "CSV file (*.csv)", "*.csv" },
                                    suggested);
            }
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::PopID();
    }

    if (!m_exportStatus.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", m_exportStatus.c_str());
    }

    ImGui::EndChild();
}
