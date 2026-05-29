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
        } else if (n->type == NodeType::Oscilloscope) {
            // Oscilloscope multi-canal: detectamos qué puertos están
            // conectados mirando edges → este nodo, y para cada uno
            // tomamos el buffer del canal correspondiente.
            auto twIt = n->params.find("Time Window");
            const float tw = (twIt != n->params.end())
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
            // Dos pases: primero detectamos puertos conectados +
            // copiamos sus buffers en heldBufs (reservado upfront
            // para que las direcciones no se invaliden); luego
            // construimos los vectores de bufs/colors/labels usando
            // punteros estables a heldBufs[i].
            const NodeDef& def = defOf(*n);
            // El codegen compacta los canales: si Oscilloscope tiene
            // conexiones solo en los puertos 1 y 3, ScilabCodeGen
            // emite los canales 0 y 1 (sin huecos), y por tanto en
            // el bridge los buffers son (id,0)=port1 y (id,1)=port3.
            // Por eso aquí mapeamos puerto-original → channel-contiguo.
            struct Conn { int port; int srcId; int channelIdx; };
            std::vector<Conn> conns;
            conns.reserve(def.inputPorts);
            int channelIdx = 0;
            for (int port = 0; port < def.inputPorts; ++port) {
                for (const auto& e : graph.edges()) {
                    if (e.toNodeId == n->id &&
                        attrInputPort(e.toAttrId) == port) {
                        conns.push_back({ port, e.fromNodeId, channelIdx });
                        ++channelIdx;
                        break;
                    }
                }
            }
            std::vector<std::vector<float>> heldBufs;
            heldBufs.reserve(conns.size());
            for (const auto& c : conns)
                heldBufs.emplace_back(bridge.buffer(n->id, c.channelIdx));

            std::vector<const std::vector<float>*> bufs;
            std::vector<ImU32>                     cols;
            std::vector<std::string>               labels;
            bufs.reserve(conns.size());
            cols.reserve(conns.size());
            labels.reserve(conns.size());
            for (size_t i = 0; i < conns.size(); ++i) {
                bufs.push_back(&heldBufs[i]);
                cols.push_back(kPalette[conns[i].port % 8]);
                // Etiqueta del canal:
                //   - custom (portLabel<N>): editado por el usuario;
                //     persiste como string en .scn.  Sin override = vacío.
                //   - unit: PRIMERO consultamos al DimensionalAnalyzer
                //     (la señal lleva su unidad encarnada desde el
                //     source).  Si el analyzer no resolvió (puerto sin
                //     contexto), caemos al portUnit<N> tipeado por el
                //     usuario.  Si tampoco hay, ningún sufijo de unidad.
                //
                // Esta inversión cierra el ciclo del análisis
                // dimensional: el Oscilloscope VALIDA visualmente que
                // las unidades están bien propagadas, en lugar de
                // pedírselas al usuario manualmente.
                char keyL[32], keyU[32];
                std::snprintf(keyL, sizeof(keyL), "portLabel%d", conns[i].port);
                std::snprintf(keyU, sizeof(keyU), "portUnit%d",  conns[i].port);
                std::string custom, unit;
                auto itL = n->stringParams.find(keyL);
                if (itL != n->stringParams.end()) custom = itL->second;

                const int inAttr = n->inputAttrId(conns[i].port);
                if (analysis.isResolved(inAttr)) {
                    scinodes::Unit u = analysis.unitAt(inAttr);
                    if (!u.isDimensionless() ||
                        std::fabs(u.magnitude - 1.0) > 1e-12) {
                        // Adimensional × 1.0 produce string vacío
                        // (sería redundante mostrar "[]").  Cualquier
                        // unidad físicamente distinguible se rinde.
                        unit = u.toCanonicalString();
                    }
                }
                if (unit.empty()) {
                    auto itU = n->stringParams.find(keyU);
                    if (itU != n->stringParams.end()) unit = itU->second;
                }

                char lab[96];
                if (!custom.empty() && !unit.empty()) {
                    std::snprintf(lab, sizeof(lab), "%s [%s]",
                                  custom.c_str(), unit.c_str());
                } else if (!custom.empty()) {
                    std::snprintf(lab, sizeof(lab), "%s", custom.c_str());
                } else {
                    const NodeInstance* srcNode = graph.findNode(conns[i].srcId);
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
                       plotW, plotH,
                       m_zoomStates[n->id],
                       bridge.solverDt() > 0 ? bridge.solverDt() : 1.0f/60.0f,
                       tw);
        } else {
            // Otros sinks single-canal (DataLogger, TerminalDisplay, …):
            // misma ruta que antes via renderWave (que internamente
            // delega a renderMultiWave con un solo canal).
            auto twIt = n->params.find("Time Window");
            const float tw = (twIt != n->params.end())
                             ? static_cast<float>(twIt->second) : 0.0f;
            scinodes::ui::plots::renderWave("##sink",
                       bridge.buffer(n->id), bridge.writeIndex(n->id),
                       plotW, plotH,
                       IM_COL32(100, 160, 230, 255),       // blue
                       m_zoomStates[n->id],
                       bridge.solverDt() > 0 ? bridge.solverDt() : 1.0f/60.0f,
                       bridge.time(),
                       tw);
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
