#include "NodeCanvas.hpp"
#include "NodeCanvasInternal.hpp"
#include "canvas/Canvas.hpp"
#include "../core/CustomNodeRegistry.hpp"
#include "../core/I18n.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// NodeCanvasPopups — split de NodeCanvas.cpp (etapa 6K.C).  Contiene la
// maquinaria del popup "Add node" (Shift+A / link-drop / edge-context):
// openAddPopup, handleLinkDropped, handleEdgeContextMenu, drawAddPopup.
// El popup soporta typeahead, browse-by-category, auto-connect tras
// drag-drop, e insert-in-edge tras right-click sobre cable.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// drawAddPopup — Blender-style Add menu at cursor
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// openAddPopup — single entry point used by Shift+A, link-dropped and
// insert-in-edge flows. screenPos is the mouse position at the moment the
// gesture was completed; the popup will spawn there and the newly created
// node (if any) will be placed at the same coordinates.
// ---------------------------------------------------------------------------
void NodeCanvas::openAddPopup(ImVec2 screenPos,
                              int autoConnectAttr,
                              int insertEdgeId) {
    m_popupPos             = screenPos;
    m_popupAutoConnectAttr = autoConnectAttr;
    m_popupInsertEdgeId    = insertEdgeId;
    m_popupSearch[0]       = '\0';
    m_popupFocusSearch     = true;
    ImGui::OpenPopup("##AddNode");
}

// ---------------------------------------------------------------------------
// handleLinkDropped — when the user drags a link from a port and drops it on
// empty canvas (no target attribute), open the add-popup with auto-connect
// state set to the originating attribute. Picking a node from the popup will
// create it and immediately wire it up.
// ---------------------------------------------------------------------------
void NodeCanvas::handleLinkDropped() {
    // (a) Detach: el usuario "agarró" un input conectado y arrastró —
    // el renderer ya transfirió el drag al output del otro extremo; aquí
    // solo borramos del modelo el edge previo.  Si el drag terminó
    // sobre otro pin, handleLinkCreated en el frame actual creará el
    // nuevo edge (efecto neto: reconexión).  Si terminó en vacío, no
    // hay LinkCreated y solo queda el detach (efecto neto: desconexión).
    int detachedId = 0;
    if (m_renderer->pollLinkDetached(detachedId)) {
        if (detachedId > 0) {
            if (m_simActive) {
                // Drag-detach durante sim = misma operación destructiva
                // que Delete sobre un cable.  Mostramos el mismo
                // mensaje que handleDeletion para coherencia.
                m_errorMsg   = "Detén la simulación para mover/quitar "
                               "esta conexión — cambia el sistema.";
                m_errorTimer = 3.5f;
            } else {
                auto before = m_graph.snapshot();
                active().removeEdge(detachedId);
                recordSnapshot(before);
                bumpDirty();
            }
        }
    }

    // (b) Drop convencional: drag desde un pin que NO partió de un
    // edge previo, soltado en vacío → abrir popup Add Node con
    // auto-connect.
    int startedAt = 0;
    if (!m_renderer->pollLinkDropped(startedAt)) return;
    if (startedAt == 0) return;
    openAddPopup(ImGui::GetMousePos(), startedAt);
}

// ---------------------------------------------------------------------------
// handleEdgeContextMenu — right-click on a link opens the add-popup with the
// edge marked for replacement. Picking a node inserts it between the edge's
// endpoints: old edge is removed, two new edges are wired (from → newNode:in0
// and newNode:out0 → to).
// ---------------------------------------------------------------------------
void NodeCanvas::handleEdgeContextMenu() {
    int hoveredLink = 0;
    if (!m_renderer->isLinkHovered(hoveredLink)) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right)) return;
    if (hoveredLink <= 0) return;
    openAddPopup(ImGui::GetMousePos(), /*autoConnect=*/0, hoveredLink);
}

void NodeCanvas::drawAddPopup() {
    ImGui::SetNextWindowPos(m_popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({240, 0}, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg,      IM_COL32( 30,  32,  36, 245));
    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32( 43,  80, 140, 200));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32( 60, 110, 190, 220));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32( 75, 130, 210, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {6, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f);

    if (ImGui::BeginPopup("##AddNode")) {
        // Header reflects the gesture that opened the popup.
        const std::string& header =
            (m_popupInsertEdgeId    != 0) ? scinodes::tr("popup.header.insert_node") :
            (m_popupAutoConnectAttr != 0) ? scinodes::tr("popup.header.connect_node") :
                                            scinodes::tr("popup.header.add_node");
        ImGui::TextDisabled("%s", header.c_str());
        ImGui::Separator();

        // Typeahead search box, auto-focused on first frame of the popup.
        if (m_popupFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_popupFocusSearch = false;
        }
        ImGui::SetNextItemWidth(-1);
        const bool searchEntered =
            ImGui::InputTextWithHint("##search",
                                     scinodes::tr("popup.search_hint").c_str(),
                                     m_popupSearch, sizeof(m_popupSearch),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
        const bool searchActive = (m_popupSearch[0] != '\0');
        ImGui::Spacing();

        // ---------------- helpers ------------------------------------------
        // Wires the newly created node into the surrounding graph as dictated
        // by the popup state (auto-connect or insert-in-edge), then resets
        // the state.  Errors are surfaced via the existing tooltip.
        auto wireNewNode = [&](int newNodeId) {
            if (m_popupAutoConnectAttr != 0) {
                // First port on the other end of the new node:
                //   came from an output → connect to its input 0;
                //   came from an input  → connect to its output 0.
                const int otherEnd = attrIsOutput(m_popupAutoConnectAttr)
                    ? (newNodeId * kAttrIdNodeStride)
                    : (newNodeId * kAttrIdNodeStride + kAttrIdOutputBase);
                auto err = active().tryAddEdge(m_popupAutoConnectAttr, otherEnd);
                if (err) {
                    m_errorMsg   = "[" + err->rule + "]  " + err->message;
                    m_errorTimer = 3.5f;
                }
            }
            if (m_popupInsertEdgeId != 0) {
                if (const Edge* e = active().findEdge(m_popupInsertEdgeId)) {
                    const int fromAttr = e->fromAttrId;
                    const int toAttr   = e->toAttrId;
                    active().removeEdge(m_popupInsertEdgeId);
                    auto e1 = active().tryAddEdge(
                        fromAttr, newNodeId * kAttrIdNodeStride);
                    auto e2 = active().tryAddEdge(
                        newNodeId * kAttrIdNodeStride + kAttrIdOutputBase, toAttr);
                    if (e1 || e2) {
                        const auto& err = e1 ? *e1 : *e2;
                        m_errorMsg   = "[" + err.rule + "]  " + err.message;
                        m_errorTimer = 3.5f;
                    }
                }
            }
            m_popupAutoConnectAttr = 0;
            m_popupInsertEdgeId    = 0;
        };

        // Common pick action used by both the categorized menus and the
        // flat search results.
        auto pickType = [&](NodeType t) {
            const int newId = addNode(t);
            wireNewNode(newId);
            ImGui::CloseCurrentPopup();
        };
        auto pickCustom = [&](const std::string& tid) {
            const int newId = addCustomNode(tid);
            wireNewNode(newId);
            ImGui::CloseCurrentPopup();
        };
        // Pick existing node as ALIAS target (etapa 6I.V).  Para el caso
        // "drag desde input" — crea un Alias apuntando al targetNodeId/
        // port y lo cablea al input.
        auto pickExistingAlias = [&](int targetNodeId, int targetPort) {
            const int newId = addNode(NodeType::Alias);
            active().setParam(newId, "target_node_id",
                              static_cast<double>(targetNodeId));
            active().setParam(newId, "target_port",
                              static_cast<double>(targetPort));
            wireNewNode(newId);
            ImGui::CloseCurrentPopup();
        };
        // Pick existing INPUT pin (caso "drag desde output").  Edge directo
        // del attr origen al input del nodo existente — sin Alias porque
        // el destino YA está en el canvas.  Reseteamos el popup state
        // antes de tryAddEdge para que wireNewNode no lo use de nuevo.
        auto pickExistingDirectInput = [&](int destNodeId, int destPort) {
            const int dstAttr = destNodeId * kAttrIdNodeStride + destPort;
            auto err = active().tryAddEdge(m_popupAutoConnectAttr, dstAttr);
            if (err) {
                m_errorMsg   = "[" + err->rule + "]  " + err->message;
                m_errorTimer = 3.5f;
            } else {
                bumpDirty();
            }
            m_popupAutoConnectAttr = 0;
            m_popupInsertEdgeId    = 0;
            ImGui::CloseCurrentPopup();
        };
        // ¿Mostrar nodos existentes en el popup?  Sí, en ambas
        // direcciones del drag — la semántica difiere:
        //   - Drag desde INPUT  ⇒ necesita un SOURCE  ⇒ lista
        //     outputs de nodos existentes ⇒ crear un Alias en el pin
        //     destino (cerca del lugar donde soltaron el cable).
        //   - Drag desde OUTPUT ⇒ necesita un CONSUMER ⇒ lista inputs
        //     de nodos existentes ⇒ crear edge directo al destino
        //     elegido (sin Alias — el destino YA existe en el canvas).
        const bool offerExisting = (m_popupAutoConnectAttr != 0);
        const bool fromInput     = offerExisting &&
                                   attrIsInput(m_popupAutoConnectAttr);
        // Ayuda para construir el label de un nodo existente.
        auto displayNodeName = [&](const NodeInstance& other) -> std::string {
            auto it = other.stringParams.find("Name");
            if (it != other.stringParams.end() && !it->second.empty())
                return it->second;
            return std::string(labelOf(other.type));
        };

        // Case-insensitive substring match.
        auto matchesSearch = [&](const std::string& label) -> bool {
            if (!searchActive) return true;
            std::string a = label;
            std::string b = m_popupSearch;
            for (char& c : a) c = (char)std::tolower((unsigned char)c);
            for (char& c : b) c = (char)std::tolower((unsigned char)c);
            return a.find(b) != std::string::npos;
        };

        // Helpers locales — el label visible viene de i18n (con fallback
        // al label del registry); el search filter compara contra el
        // label traducido para que un usuario en español pueda buscar
        // "señal" y encontrar Step Signal traducido a "Señal escalón".
        auto displayLabel = [&](NodeType t, const std::string& fallback) {
            return scinodes::trOr(std::string("node.") + typeName(t) + ".label",
                                  fallback);
        };
        auto displayDesc  = [&](NodeType t, const std::string& fallback) {
            return scinodes::trOr(std::string("node.") + typeName(t) + ".description",
                                  fallback);
        };
        // Tooltip de descripción con wrap a ~320 px para que las
        // descripciones largas (SubGraph, PIDController, …) no se
        // muestren en una sola línea fuera del viewport.  Sólo aparece
        // mientras el usuario tenga Ctrl presionado — regla unificada
        // "Ctrl para más info" del editor (comentarios de nodos,
        // descripciones del menú "Añadir nodo", etc).  Sin Ctrl los
        // tooltips no interrumpen la lectura del menú.
        auto descTooltip = [&](const std::string& text) {
            if (text.empty()) return;
            if (!ImGui::GetIO().KeyCtrl) return;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

        auto menuItem = [&](NodeType t) {
            const NodeDef& d = nodeRegistry().at(t);
            const std::string lbl = displayLabel(t, d.label);
            if (ImGui::MenuItem(lbl.c_str())) pickType(t);
            if (ImGui::IsItemHovered()) descTooltip(displayDesc(t, d.description));
        };

        // -------- Flat typeahead result list (replaces categorized view) ---
        if (searchActive) {
            int shown = 0;
            std::optional<NodeType>  firstBuiltin;
            std::optional<std::string> firstCustom;

            for (const auto& [t, d] : nodeRegistry()) {
                const std::string lbl = displayLabel(t, d.label);
                if (!matchesSearch(lbl)) continue;
                if (!firstBuiltin && !firstCustom) firstBuiltin = t;
                if (ImGui::MenuItem(lbl.c_str())) pickType(t);
                if (ImGui::IsItemHovered())
                    descTooltip(displayDesc(t, d.description));
                ++shown;
            }
            for (const auto& tid : scinodes::customNodes().typeIds()) {
                const auto* cd = scinodes::customNodes().find(tid);
                if (!cd) continue;
                if (!matchesSearch(cd->label)) continue;
                if (!firstBuiltin && !firstCustom) firstCustom = tid;
                if (ImGui::MenuItem(cd->label.c_str())) pickCustom(tid);
                if (ImGui::IsItemHovered()) descTooltip(cd->description);
                ++shown;
            }
            // Nodos existentes (etapa 6I.V): según la dirección del drag.
            if (offerExisting) {
                bool sepShown = false;
                for (const NodeInstance& other : active().nodes()) {
                    if (isAliasType(other.type)) continue;
                    const NodeDef& d = defOf(other);
                    const std::string nm = displayNodeName(other);
                    if (!matchesSearch(nm)) continue;
                    // From-INPUT: listar outputs.  From-OUTPUT: listar inputs.
                    const int portCount = fromInput ? d.outputPorts : d.inputPorts;
                    if (portCount <= 0) continue;
                    for (int p = 0; p < portCount; ++p) {
                        const auto& labels = fromInput ? d.outputPortLabels
                                                       : d.inputPortLabels;
                        std::string portLbl;
                        if (labels.size() > (size_t)p && !labels[p].empty())
                            portLbl = labels[p];
                        else
                            portLbl = (fromInput ? "out " : "in ")
                                    + std::to_string(p + 1);
                        const std::string lbl = "→ " + nm + "  [" + portLbl + "]";
                        if (!sepShown) {
                            ImGui::Separator();
                            ImGui::TextDisabled(" Nodos en el canvas");
                            sepShown = true;
                        }
                        ImGui::PushID(other.id * 1000 + p);
                        if (ImGui::MenuItem(lbl.c_str())) {
                            if (fromInput) pickExistingAlias(other.id, p);
                            else           pickExistingDirectInput(other.id, p);
                        }
                        ImGui::PopID();
                        ++shown;
                    }
                }
            }
            if (shown == 0) {
                ImGui::TextDisabled("  no matches");
            }
            // Enter on the search box picks the first match.
            if (searchEntered) {
                if (firstBuiltin)     pickType(*firstBuiltin);
                else if (firstCustom) pickCustom(*firstCustom);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140,140,140,180));
            ImGui::TextUnformatted("  Enter: pick first   Esc: close");
            ImGui::PopStyleColor();
            ImGui::EndPopup();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
            return;
        }
        // -------- end flat typeahead ---------------------------------------

        // Etiqueta de categoría: sangría + nombre traducido.  Inline
        // (sin lambda wrapper) para que el audit i18n detecte cada key
        // como usada por el código — un wrapper alrededor de tr()
        // ocultaría las strings al regex de extracción de
        // tools/i18n_coverage.py.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 90, 200, 110, 255));
        bool srcOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.sources")).c_str());
        ImGui::PopStyleColor();
        if (srcOpen) {
            for (NodeType t : { NodeType::VoltageSource, NodeType::CurrentSource,
                                NodeType::StepSignal, NodeType::SineSignal,
                                NodeType::RampSignal,
                                NodeType::Alias })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Etapa 6I.V: sección "Nodos en el canvas".  Aparece en ambas
        // direcciones del drag:
        //   from-INPUT  → outputs existentes → crea Alias.
        //   from-OUTPUT → inputs existentes  → edge directo (sin alias).
        if (offerExisting && active().nodeCount() > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 160,  90, 255));
            bool exOpen = ImGui::BeginMenu(
                ("  " + scinodes::tr("popup.category.existing")).c_str());
            ImGui::PopStyleColor();
            if (exOpen) {
                bool anyShown = false;
                for (const NodeInstance& other : active().nodes()) {
                    if (isAliasType(other.type)) continue;
                    const NodeDef& d = defOf(other);
                    const int portCount = fromInput ? d.outputPorts : d.inputPorts;
                    if (portCount <= 0) continue;
                    const std::string nm = displayNodeName(other);
                    for (int p = 0; p < portCount; ++p) {
                        const auto& labels = fromInput ? d.outputPortLabels
                                                       : d.inputPortLabels;
                        std::string portLbl;
                        if (labels.size() > (size_t)p && !labels[p].empty())
                            portLbl = labels[p];
                        else
                            portLbl = (fromInput ? "out " : "in ")
                                    + std::to_string(p + 1);
                        const std::string lbl = "→ " + nm + "  [" + portLbl + "]";
                        ImGui::PushID(other.id * 1000 + p);
                        if (ImGui::MenuItem(lbl.c_str())) {
                            if (fromInput) pickExistingAlias(other.id, p);
                            else           pickExistingDirectInput(other.id, p);
                        }
                        ImGui::PopID();
                        anyShown = true;
                    }
                }
                if (!anyShown) {
                    const char* msg = fromInput
                        ? "  (no hay outputs disponibles)"
                        : "  (no hay inputs disponibles)";
                    ImGui::TextDisabled("%s", msg);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 90, 130, 220, 255));
        bool txOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.transformers")).c_str());
        ImGui::PopStyleColor();
        if (txOpen) {
            for (NodeType t : { NodeType::Gain, NodeType::Summation,
                                NodeType::Integrator, NodeType::Differentiator,
                                NodeType::LowPassFilter, NodeType::PIDController,
                                NodeType::TransferFunction,
                                NodeType::TransferFunction2,
                                NodeType::Saturation,
                                NodeType::GearTransmission,
                                NodeType::InverseKinematics,
                                NodeType::DegToRad,
                                NodeType::RadToDeg })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Devices — categoría gramatical Device.  Comportamiento de
        // transformador en R1-R5 pero llevan modelo 3-D asociado vía
        // contrato (sec. geometry-contracts).  Coloreado púrpura.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160,  90, 200, 255));
        bool dvOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.devices")).c_str());
        ImGui::PopStyleColor();
        if (dvOpen) {
            for (NodeType t : { NodeType::DCMotorModel })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Sizing & Electromagnetics (v0.8) — multiphysics-design nodes.
        // Coloured purple to distinguish from generic Source/Transformer/Sink.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 120, 220, 255));
        bool szOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.sizing")).c_str());
        ImGui::PopStyleColor();
        if (szOpen) {
            for (NodeType t : { NodeType::DesignTemplate,
                                NodeType::PMSMSizing,
                                NodeType::IPMSizing,
                                NodeType::BLDCSizing,
                                NodeType::PMSMElectromagnetic,
                                NodeType::AirgapFluxDensity,
                                NodeType::PMSMEfficiency })
                menuItem(t);
            ImGui::EndMenu();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220,  80,  80, 255));
        bool snkOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.sinks")).c_str());
        ImGui::PopStyleColor();
        if (snkOpen) {
            for (NodeType t : { NodeType::Oscilloscope, NodeType::FFTAnalyzer,
                                NodeType::PhasePortrait, NodeType::DataLogger,
                                NodeType::TerminalDisplay, NodeType::View3DSink,
                                NodeType::View3DThermalSink,
                                NodeType::View3DDeformationSink,
                                NodeType::HeatmapSink,
                                NodeType::DistributionSink })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Structural & NVH (v1.0) — Maxwell forces + modal frequencies.
        // Pink-tinted so it reads distinct from Thermal's orange.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 110, 170, 255));
        bool nvhOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.structural")).c_str());
        ImGui::PopStyleColor();
        if (nvhOpen) {
            for (NodeType t : { NodeType::MaxwellForce,
                                NodeType::ModalFrequency,
                                NodeType::TolerancePerturbator })
                menuItem(t);
            ImGui::EndMenu();
        }

        // Thermal Network (v0.9) — losses + lumped RC nodes. Coloured
        // orange to read as "heat".
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 140,  60, 255));
        bool thOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.thermal")).c_str());
        ImGui::PopStyleColor();
        if (thOpen) {
            for (NodeType t : { NodeType::JouleLoss,
                                NodeType::CoreLoss,
                                NodeType::MechanicalLoss,
                                NodeType::ThermalMass,
                                NodeType::ThermalNode,
                                NodeType::ThermalResistance,
                                NodeType::CoolingSystem,
                                NodeType::ConvectiveCooling })
                menuItem(t);
            ImGui::EndMenu();
        }

        // 3D Scene — sub-lenguaje Geometry del grafo (R6 los separa de
        // los nodos de Signal).  Object3D referencia el catálogo del
        // proyecto, TransformObject es el bridge bilingüe, SceneOutput
        // colecta lo que el panel View3D rendera.  Cyan-tinted para
        // leerse distinto de cualquier otra categoría.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32( 80, 200, 200, 255));
        bool scOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.scene_3d")).c_str());
        ImGui::PopStyleColor();
        if (scOpen) {
            for (NodeType t : { NodeType::Object3D,
                                NodeType::TransformObject,
                                NodeType::SceneOutput,
                                NodeType::Vec3Constant,
                                NodeType::CombineXYZ,
                                NodeType::SeparateXYZ,
                                NodeType::VectorAdd,
                                NodeType::VectorSub,
                                NodeType::VectorScale,
                                NodeType::VectorDot,
                                NodeType::VectorCross,
                                NodeType::VectorLength,
                                NodeType::VectorNormalize })
                menuItem(t);
            ImGui::EndMenu();
        }

        // ---- Custom (JSON-loaded) types ---------------------------------
        auto customIds = scinodes::customNodes().typeIds();
        std::sort(customIds.begin(), customIds.end());

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 160, 80, 255));
        bool cusOpen = ImGui::BeginMenu(
            ("  " + scinodes::tr("popup.category.custom")).c_str());
        ImGui::PopStyleColor();
        if (cusOpen) {
            if (customIds.empty()) {
                ImGui::TextDisabled("  %s",
                    scinodes::tr("popup.custom.empty").c_str());
            } else {
                for (const auto& tid : customIds) {
                    const auto* cd =
                        scinodes::customNodes().find(tid);
                    if (!cd) continue;
                    if (ImGui::MenuItem(cd->label.c_str())) pickCustom(tid);
                    if (ImGui::IsItemHovered() && ImGui::GetIO().KeyCtrl &&
                        !cd->description.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.f);
                        ImGui::TextUnformatted(cd->description.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
            }
            ImGui::Separator();
            bool busy = m_loadCustomDialog.isOpen();
            ImGui::BeginDisabled(busy);
            if (ImGui::MenuItem(scinodes::tr("popup.custom.load_from_json").c_str())) {
                m_loadCustomDialog.open(
                    FileDialog::Mode::Open,
                    scinodes::tr("dialog.load_custom_node"),
                    { "JSON descriptor (*.json)", "*.json" });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        // Status del load custom (no es un "hint" sino feedback de la
        // última operación de carga; aparece solo cuando hay mensaje).
        if (!m_customLoadStatus.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("  %s", m_customLoadStatus.c_str());
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

// ---------------------------------------------------------------------------
// drawFindPopup (etapa 6M) — Shift+B abre un buscador de nodos por nombre.
// Recursivo: walks top + todos los SubGraphs anidados.  Cada hit muestra
// nombre + breadcrumb del path (`Top › Lazo › PID #7`).  Enter sobre el
// primer hit, o click sobre cualquiera, navega + centra cámara.
// ---------------------------------------------------------------------------
void NodeCanvas::drawFindPopup() {
    if (!m_findOpen) return;

    constexpr const char* kPopupId = "##findNodePopup";
    if (!ImGui::IsPopupOpen(kPopupId))
        ImGui::OpenPopup(kPopupId);

    ImGui::SetNextWindowPos(m_popupPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({ 380, 0 }, ImGuiCond_Always);
    if (ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove)) {
        ImGui::TextDisabled("%s", scinodes::tr("find.header").c_str());
        ImGui::Separator();

        if (m_findFocusPending) {
            ImGui::SetKeyboardFocusHere();
            m_findFocusPending = false;
        }
        ImGui::SetNextItemWidth(-1.f);
        const bool enterSubmitted = ImGui::InputTextWithHint(
            "##find", scinodes::tr("find.search_hint").c_str(),
            m_findBuf, sizeof(m_findBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        const std::string query = m_findBuf;
        auto hits = searchNodes(query);

        ImGui::Separator();
        if (hits.empty()) {
            ImGui::TextDisabled("%s",
                                scinodes::tr("find.no_results").c_str());
        } else {
            // Limitar visibles para no inflar la lista en grafos enormes.
            constexpr int kMaxShown = 32;
            const int shown = std::min<int>(kMaxShown, (int)hits.size());
            for (int i = 0; i < shown; ++i) {
                const auto& h = hits[i];
                ImGui::PushID(i);
                if (ImGui::Selectable(h.displayName.c_str(),
                                      /*selected=*/false,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    focusNode(h);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextDisabled("    %s", h.breadcrumb.c_str());
                ImGui::PopID();
            }
            if ((int)hits.size() > kMaxShown) {
                ImGui::TextDisabled(
                    scinodes::tr("find.more_results").c_str(),
                    (int)hits.size() - kMaxShown);
            }
        }

        // Enter sobre el InputText: si hay al menos un hit, navega al
        // primero — atajo común para "abrir el más probable".
        if (enterSubmitted && !hits.empty()) {
            focusNode(hits.front());
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_findOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        // El popup se cerró (click fuera, etc.) — sincronizar el flag.
        m_findOpen = false;
    }
}
