#include "PlotPanel.hpp"
#include "../core/CsvExport.hpp"
#include "../core/DimensionalAnalyzer.hpp"
#include "../core/Fft.hpp"
#include "../core/I18n.hpp"
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

// Devuelve el label traducido de un tipo de nodo (clave i18n
// `node.<typeName>.label`), con fallback al `labelOf()` del registry
// cuando no hay traducción.  Mismo patrón que usa NodePalette para los
// títulos de la paleta — extraerlo aquí asegura que el plot panel
// hable el mismo idioma que el resto de la UI.
static std::string trNodeLabel(NodeType t) {
    return scinodes::trOr(
        std::string("node.") + typeName(t) + ".label",
        labelOf(t));
}

static bool hasIncomingEdge(int nodeId, const NodeGraph& g) {
    for (const auto& e : g.edges())
        if (e.toNodeId == nodeId) return true;
    return false;
}

// -------------------------------------------------------------------------
// Renderers movidos a src/ui/plots/<Name>Renderer.{hpp,cpp}.
// -------------------------------------------------------------------------

void PlotPanel::drawContent(const NodeGraph& graph,
                            const scinodes::ISimSession& bridge) {
    // ---- Análisis dimensional para auto-labels (etapa 6I.J) ---------------
    // Resolvemos las unidades de todos los puertos una sola vez por frame.
    // El Oscilloscope (y cualquier sink futuro) consulta `analysis.unitAt(...)`
    // en lugar de pedir al usuario que tipee la unidad por canal — si el
    // analyzer ya conoce que la señal es "rad/s", el plot lo muestra solo.
    // Para grafos del orden de E1 (≤ 20 nodos), el coste es < 0.1 ms por
    // frame (medido en tests/test_grammar perf section).
    const scinodes::DimensionalAnalysis analysis = scinodes::analyzeUnits(graph);

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
        const std::string& msg = scinodes::tr("plots.no_plots_hint");
        float tw = ImGui::CalcTextSize(msg.c_str()).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
        ImGui::TextUnformatted(msg.c_str());
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
        char label[96];
        std::snprintf(label, sizeof(label), "%s  #%d",
                      trNodeLabel(n->type).c_str(), n->id);

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

        PlotCtx ctx{ *n, graph, bridge, analysis, plotW, plotH };
        if (PlotDrawFn fn = lookupPlotDrawer(n->type)) {
            (this->*fn)(ctx);
        } else {
            drawWaveDefault(ctx);
        }


        // ---- Export button for DataLogger sinks --------------------------
        if (n->type == NodeType::DataLogger) {
            bool busy = m_exportDialog.isOpen() || m_pendingExportSinkId != 0;
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton(scinodes::tr("plots.export_csv").c_str())) {
                m_pendingExportSinkId = n->id;
                char lbl[96];
                std::snprintf(lbl, sizeof(lbl), "%s #%d",
                              trNodeLabel(n->type).c_str(), n->id);
                m_pendingExportLabel = lbl;
                char suggested[64];
                std::snprintf(suggested, sizeof(suggested),
                              "scinodes_logger_%d.csv", n->id);
                m_exportDialog.open(FileDialog::Mode::Save,
                                    scinodes::tr("dialog.export.logger_csv"),
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

// ---------------------------------------------------------------------------
// Drawers por tipo de sink (etapa 6J.7).
//
// Cada uno es la implementación de UN nodo de la gramática de plots.
// Comparten signature `(const PlotCtx&)` para que `lookupPlotDrawer`
// pueda devolver el puntero correcto vía la tabla NodeType→fn.
// ---------------------------------------------------------------------------

void PlotPanel::drawSpectrum(const PlotCtx& ctx) {
    auto it = ctx.n.params.find("Bin Count");
    int binCount = (it != ctx.n.params.end()) ? (int)it->second : 256;
    scinodes::ui::plots::renderSpectrum("##fft",
                   ctx.bridge.buffer(ctx.n.id),
                   ctx.bridge.writeIndex(ctx.n.id),
                   binCount, ctx.plotW, ctx.plotH,
                   IM_COL32(230, 160, 100, 255));   // orange
}

void PlotPanel::drawPhase(const PlotCtx& ctx) {
    scinodes::ui::plots::renderPhase(
        ctx.bridge.buffer(ctx.n.id, 0), ctx.bridge.writeIndex(ctx.n.id, 0),
        ctx.bridge.buffer(ctx.n.id, 1), ctx.bridge.writeIndex(ctx.n.id, 1),
        ctx.plotW, ctx.plotH,
        IM_COL32(180, 230, 130, 255),                 // green
        m_zoomStates[ctx.n.id]);
}

void PlotPanel::drawHeatmap(const PlotCtx& ctx) {
    scinodes::ui::plots::renderHeatmap(
        ctx.bridge.buffer(ctx.n.id, 0), ctx.bridge.writeIndex(ctx.n.id, 0),
        ctx.bridge.buffer(ctx.n.id, 1), ctx.bridge.writeIndex(ctx.n.id, 1),
        ctx.bridge.buffer(ctx.n.id, 2), ctx.bridge.writeIndex(ctx.n.id, 2),
        ctx.plotW, ctx.plotH);
}

void PlotPanel::drawHistogram(const PlotCtx& ctx) {
    auto it = ctx.n.params.find("Bin Count");
    int bins = (it != ctx.n.params.end()) ? (int)it->second : 20;
    scinodes::ui::plots::renderHistogram(
        ctx.bridge.buffer(ctx.n.id), ctx.bridge.writeIndex(ctx.n.id),
        bins, ctx.plotW, ctx.plotH,
        IM_COL32(220, 110, 170, 220));   // structural-pink
}

void PlotPanel::drawOscilloscope(const PlotCtx& ctx) {
    // Oscilloscope multi-canal: detectamos qué puertos están conectados
    // mirando edges → este nodo, y para cada uno tomamos el buffer del
    // canal correspondiente.  El codegen compacta los canales: si
    // Oscilloscope tiene conexiones solo en los puertos 1 y 3, en el
    // bridge los buffers son (id,0)=port1 y (id,1)=port3.  Acá mapeamos
    // puerto-original → channel-contiguo.
    auto twIt = ctx.n.params.find("Time Window");
    const float tw = (twIt != ctx.n.params.end())
                     ? static_cast<float>(twIt->second) : 0.0f;
    static const ImU32 kPalette[8] = {
        IM_COL32(100, 160, 230, 255),  // azul
        IM_COL32(230, 160, 100, 255),  // naranja
        IM_COL32(120, 210, 130, 255),  // verde
        IM_COL32(220, 110, 170, 220),  // rosa
        IM_COL32(230, 215,  90, 255),  // amarillo
        IM_COL32(170, 130, 220, 255),  // morado
        IM_COL32( 90, 215, 215, 255),  // cyan
        IM_COL32(215, 100, 100, 255),  // rojo
    };
    const NodeDef& def = defOf(ctx.n);
    struct Conn { int port; int srcId; int channelIdx; };
    std::vector<Conn> conns;
    conns.reserve(def.inputPorts);
    int channelIdx = 0;
    for (int port = 0; port < def.inputPorts; ++port) {
        for (const auto& e : ctx.graph.edges()) {
            if (e.toNodeId == ctx.n.id &&
                attrInputPort(e.toAttrId) == port) {
                conns.push_back({ port, e.fromNodeId, channelIdx });
                ++channelIdx;
                break;
            }
        }
    }
    // Buffers held in storage estable para que los punteros no se invaliden.
    std::vector<std::vector<float>> heldBufs;
    heldBufs.reserve(conns.size());
    for (const auto& c : conns)
        heldBufs.emplace_back(ctx.bridge.buffer(ctx.n.id, c.channelIdx));

    std::vector<const std::vector<float>*> bufs;
    std::vector<ImU32>                     cols;
    std::vector<std::string>               labels;
    bufs.reserve(conns.size());
    cols.reserve(conns.size());
    labels.reserve(conns.size());
    for (size_t i = 0; i < conns.size(); ++i) {
        bufs.push_back(&heldBufs[i]);
        cols.push_back(kPalette[conns[i].port % 8]);
        // Etiqueta del canal: portLabel<N> (custom user-edited) +
        // unidad inferida por el DimensionalAnalyzer (con fallback a
        // portUnit<N> tipeado por el usuario).
        char keyL[32], keyU[32];
        std::snprintf(keyL, sizeof(keyL), "portLabel%d", conns[i].port);
        std::snprintf(keyU, sizeof(keyU), "portUnit%d",  conns[i].port);
        std::string custom, unit;
        auto itL = ctx.n.stringParams.find(keyL);
        if (itL != ctx.n.stringParams.end()) custom = itL->second;

        const int inAttr = ctx.n.inputAttrId(conns[i].port);
        if (ctx.analysis.isResolved(inAttr)) {
            scinodes::Unit u = ctx.analysis.unitAt(inAttr);
            if (!u.isDimensionless() ||
                std::fabs(u.magnitude - 1.0) > 1e-12) {
                unit = u.toCanonicalString();
            }
        }
        if (unit.empty()) {
            auto itU = ctx.n.stringParams.find(keyU);
            if (itU != ctx.n.stringParams.end()) unit = itU->second;
        }

        char lab[96];
        if (!custom.empty() && !unit.empty()) {
            std::snprintf(lab, sizeof(lab), "%s [%s]",
                          custom.c_str(), unit.c_str());
        } else if (!custom.empty()) {
            std::snprintf(lab, sizeof(lab), "%s", custom.c_str());
        } else {
            const NodeInstance* srcNode = ctx.graph.findNode(conns[i].srcId);
            const std::string& inPrefix = scinodes::tr("plots.in_prefix");
            if (srcNode)
                std::snprintf(lab, sizeof(lab), "%s %d: %s#%d",
                              inPrefix.c_str(),
                              conns[i].port + 1,
                              trNodeLabel(srcNode->type).c_str(),
                              conns[i].srcId);
            else
                std::snprintf(lab, sizeof(lab), "%s %d",
                              inPrefix.c_str(),
                              conns[i].port + 1);
        }
        labels.emplace_back(lab);
    }
    scinodes::ui::plots::renderMultiWave("##sink",
               bufs, cols, labels,
               ctx.plotW, ctx.plotH,
               m_zoomStates[ctx.n.id],
               ctx.bridge.solverDt() > 0 ? ctx.bridge.solverDt() : 1.0f/60.0f,
               tw);
}

void PlotPanel::drawWaveDefault(const PlotCtx& ctx) {
    // Sinks single-canal sin drawer dedicado (DataLogger, TerminalDisplay,
    // View3DSink, …).  renderWave delega internamente a renderMultiWave
    // con un solo canal — misma ruta histórica.
    auto twIt = ctx.n.params.find("Time Window");
    const float tw = (twIt != ctx.n.params.end())
                     ? static_cast<float>(twIt->second) : 0.0f;
    scinodes::ui::plots::renderWave("##sink",
               ctx.bridge.buffer(ctx.n.id),
               ctx.bridge.writeIndex(ctx.n.id),
               ctx.plotW, ctx.plotH,
               IM_COL32(100, 160, 230, 255),       // blue
               m_zoomStates[ctx.n.id],
               ctx.bridge.solverDt() > 0 ? ctx.bridge.solverDt() : 1.0f/60.0f,
               ctx.bridge.time(),
               tw);
}

// Tabla maestra: NodeType → drawer method.  Single source of truth para
// "este sink-type tiene plot dedicado".  Sinks sin entrada caen al
// `drawWaveDefault` via la rama else del dispatcher.
PlotPanel::PlotDrawFn PlotPanel::lookupPlotDrawer(NodeType t) {
    static const std::unordered_map<NodeType, PlotDrawFn> kDrawers = {
        { NodeType::FFTAnalyzer,      &PlotPanel::drawSpectrum     },
        { NodeType::PhasePortrait,    &PlotPanel::drawPhase        },
        { NodeType::HeatmapSink,      &PlotPanel::drawHeatmap      },
        { NodeType::DistributionSink, &PlotPanel::drawHistogram    },
        { NodeType::Oscilloscope,     &PlotPanel::drawOscilloscope },
    };
    if (auto it = kDrawers.find(t); it != kDrawers.end()) return it->second;
    return nullptr;
}
