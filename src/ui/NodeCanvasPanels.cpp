#include "NodeCanvas.hpp"
#include "NodeCanvasInternal.hpp"
#include "canvas/Canvas.hpp"
#include "../app/AssetService.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/CsvParamIO.hpp"
#include "../core/CustomNodeRegistry.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/DimensionalAnalyzer.hpp"
#include "../core/I18n.hpp"
#include "../core/Quantity.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// NodeCanvasPanels — split de NodeCanvas.cpp (etapa 6K.C).  Contiene los
// paneles flotantes y popups de edición: drawParamPanel (panel de
// parámetros del nodo seleccionado, con secciones para Alias, Custom JSON,
// Oscilloscope multi-canal, etc.) y drawRenamePopup (F2 — edita Name +
// Comment del nodo).  La lógica de input dispatch sigue en NodeCanvas.cpp.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Floating parameter panel — one DragFloat per param, plus a close button.
// Undo behaves the same as inline editing: snapshot on activate, commit on
// deactivate. Disabled when the canvas is in read-only mode.
// ---------------------------------------------------------------------------
void NodeCanvas::drawParamPanel() {
    if (m_openParamPanelNodeId == 0) return;
    const NodeInstance* n = active().findNode(m_openParamPanelNodeId);
    if (!n) { m_openParamPanelNodeId = 0; return; }
    const NodeDef& def = defOf(*n);

    const std::string nodeLbl = scinodes::trOr(
        std::string("node.") + typeName(n->type) + ".label", def.label);
    char title[80];
    std::snprintf(title, sizeof(title), "%s  #%d###paramPanel",
                  nodeLbl.c_str(), n->id);

    ImGui::SetNextWindowPos(m_paramPanelPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Descripción wrappeada — sin esto AlwaysAutoResize hacía el
        // panel tan ancho como la oración más larga (los nodos del
        // sub-grafo Geometry tienen descripciones de ~150 chars).
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 310);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", scinodes::trOr(
            std::string("node.") + typeName(n->type) + ".description",
            def.description).c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        // ---- Import / Export row (only if the node has any params) ----
        // Alias (etapa 6I.U): sus params (target_node_id, target_port) son
        // identificadores no numéricos.  Bulk-edit via CSV no aplica.
        const bool hasCsvParams = !def.params.empty()
                                  && n->type != NodeType::Alias;
        if (hasCsvParams) {
            bool busy = m_paramCsvDialog.isOpen() ||
                        m_paramCsvAction != ParamCsvAction::None;
            ImGui::BeginDisabled(busy);

            if (ImGui::SmallButton("Import CSV…")) {
                m_paramCsvAction = ParamCsvAction::Import;
                m_paramCsvNodeId = n->id;
                m_paramCsvDialog.open(FileDialog::Mode::Open,
                                      "Import parameters from CSV",
                                      { "CSV file (*.csv)", "*.csv" });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Export CSV…")) {
                m_paramCsvAction = ParamCsvAction::Export;
                m_paramCsvNodeId = n->id;
                char suggested[64];
                std::snprintf(suggested, sizeof(suggested),
                              "params_%s_%d.csv",
                              typeName(n->type), n->id);
                m_paramCsvDialog.open(FileDialog::Mode::Save,
                                      "Export parameters to CSV",
                                      { "CSV file (*.csv)", "*.csv" },
                                      suggested);
            }
            ImGui::EndDisabled();
            if (!m_paramCsvStatus.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_paramCsvStatus.c_str());
            }
        }

        // ---- Asset 3D legacy (PATH A) — sólo informativo / quitar ---------
        // Desde el refactor del sub-grafo de escena (`Object3D` +
        // `TransformObject` + `SceneOutput`), los Device nodes ya NO
        // poseen su asset 3D — la geometría vive en el catálogo del
        // proyecto y se referencia desde Object3D.  La sección sólo
        // aparece cuando el nodo arrastra un `assetPath` heredado de un
        // .scn 0.4 legacy.  La acción ofrecida es QUITAR el legacy y
        // un texto que dirige al usuario al flujo nuevo — no hay forma
        // desde la UI de re-cargar un asset al Device.
        if (def.category == NodeCategory::Device && !n->assetPath.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Asset 3D heredado (legacy)");

            const auto* contract =
                (m_contractRegistry ? m_contractRegistry->find(typeName(n->type)) : nullptr);

            ImGui::TextWrapped("%s", n->assetPath.c_str());

            if (contract) {
                const scinodes::DeviceAsset* asset =
                    m_assetService ? m_assetService->find(n->id) : nullptr;
                if (!asset) {
                    reloadAssetFor(n->id);
                    asset = m_assetService ? m_assetService->find(n->id) : nullptr;
                }
                if (asset) {
                    if (asset->valid()) {
                        ImGui::TextColored({0.3f, 0.9f, 0.5f, 1.0f},
                            "✓ Cumple contrato '%s'",
                            contract->device_type.c_str());
                    } else {
                        ImGui::TextColored({0.95f, 0.5f, 0.3f, 1.0f},
                            "✗ Faltan elementos del contrato:");
                        for (const auto& m : asset->missing) {
                            ImGui::BulletText("%s", m.c_str());
                        }
                    }
                    if (!asset->warnings.empty()) {
                        ImGui::TextDisabled("Advertencias:");
                        for (const auto& w : asset->warnings) {
                            ImGui::BulletText("%s", w.c_str());
                        }
                    }
                }
            } else {
                ImGui::TextDisabled("(sin contrato registrado para %s)",
                                    typeName(n->type));
            }

            ImGui::TextDisabled("Para asignar geometría a este device usá "
                                "el sub-grafo de escena: Archivo → Importar "
                                "modelo 3D, luego Object3D + Transform Object "
                                "+ Scene Output.");
            if (ImGui::SmallButton("Quitar asset heredado")) {
                active().setAssetPath(n->id, "");
                if (m_assetService) m_assetService->detach(n->id);
            }
        }

        ImGui::Separator();

        if (m_readOnly) ImGui::BeginDisabled();

        for (int i = 0; i < (int)def.params.size(); ++i) {
            const auto& pd = def.params[i];
            float val = (float)n->params.at(pd.name);

            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            // Label traducido (clave node.<type>.param.<paramId>),
            // fallback al pd.name literal.
            const std::string paramLbl = scinodes::trOr(
                std::string("node.") + typeName(n->type) + ".param." + pd.name,
                pd.name);
            ImGui::TextUnformatted(paramLbl.c_str());
            ImGui::SameLine(160.f);
            ImGui::SetNextItemWidth(120.f);

            const ImGuiID wid = ImGui::GetID("##v");
            const bool changed = ImGui::DragFloat("##v", &val, 0.01f,
                                                  0.f, 0.f, "%.4g");
            // En modo text-input (Ctrl-click o doble-click → escritura
            // dígito a dígito) cada keystroke dispara "changed", lo que
            // mandaría valores intermedios al solver (escribir "50"
            // pasaría primero por "5").  Solo enviamos al solver en
            // modo drag (mouse arrastrando); en text-input esperamos
            // al commit (Enter o focus loss) — IsItemDeactivatedAfterEdit.
            const bool isTextInput = ImGui::TempInputIsActive(wid);

            if (ImGui::IsItemActivated())
                m_pendingParamBefore = m_graph.snapshot();

            if (changed) {
                // Drag mode: mutamos el modelo + notificamos al solver
                // por cada delta — el usuario espera feedback fluido.
                // Text-input mode: ImGui mantiene el buffer interno con
                // el valor en edición; NO mutamos el modelo ni mandamos
                // al solver hasta IsItemDeactivatedAfterEdit (Enter o
                // focus loss).  Sin esto, escribir "1.5708" pasaba por
                // "1", "15", "157", "1570", "15708" como valores
                // intermedios — visible inmediatamente en el render
                // del sub-grafo de escena (que evalúa cada frame).
                if (!isTextInput) {
                    active().setParam(n->id, pd.name, (double)val);
                    if (m_paramCallback)
                        m_paramCallback(pathFor(n->id), i, (double)val);
                }
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                // Commit final tras text-input (Enter o focus loss) o
                // tras un drag completo.  Mutamos modelo + solver una
                // sola vez con el valor definitivo.  En drag mode
                // muchas de estas escrituras serán redundantes con la
                // última actualización del bloque `changed`; el costo
                // es trivial y la simetría worth it.
                active().setParam(n->id, pd.name, (double)val);
                if (m_paramCallback) m_paramCallback(pathFor(n->id), i, (double)val);
                if (m_pendingParamBefore) {
                    recordSnapshot(*m_pendingParamBefore);
                    m_pendingParamBefore = std::nullopt;
                }
            }

            if (!pd.unit.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", pd.unit.c_str());
            }
            ImGui::PopID();
        }

        if (m_readOnly) ImGui::EndDisabled();

        // -----------------------------------------------------------------
        // Object3D — combo "objectRef" alimentado por el catálogo del
        // proyecto (NodeGraph::importedObjects()).  Cada entrada del
        // combo es "<objectName>" o "<objectName>/<partName>".  Sin esto
        // los Object3D recién creados no saben qué del catálogo
        // referenciar y el View3DPanel los rendera como placeholder.
        // -----------------------------------------------------------------
        if (n->type == NodeType::Object3D) {
            ImGui::Separator();
            ImGui::TextDisabled("Referencia al catálogo");
            const std::string curRef =
                n->stringParams.count("objectRef")
                    ? n->stringParams.at("objectRef")
                    : std::string{};

            // Build options: "<name>" + "<name>/<partName>" por cada part.
            std::vector<std::string> options;
            options.push_back("");   // <ninguno> — limpia el ref
            for (const auto& obj : m_graph.importedObjects()) {
                options.push_back(obj.name);
                for (const auto& part : obj.parts)
                    options.push_back(obj.name + "/" + part);
            }

            const char* preview = curRef.empty() ? "(sin asignar)" : curRef.c_str();
            ImGui::SetNextItemWidth(260.f);
            if (ImGui::BeginCombo("##objectRef", preview)) {
                for (const auto& opt : options) {
                    const bool sel = (opt == curRef);
                    const char* lbl = opt.empty() ? "(sin asignar)" : opt.c_str();
                    if (ImGui::Selectable(lbl, sel))
                        active().setStringParam(n->id, "objectRef", opt);
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (m_graph.importedObjects().empty()) {
                ImGui::TextDisabled("  (catálogo vacío — Archivo → Importar modelo 3D)");
            }
        }

        // -----------------------------------------------------------------
        // Alias (etapa 6I.U): selector de "referenciar a..." — dropdown
        // con todos los nodos del grafo, agrupados por nodo y mostrando
        // sus output ports.  Al elegir, se persisten target_node_id y
        // target_port en params.  Sin asignación, el alias emite 0.0
        // y el analyzer dimensional NO puede resolver su unit.
        // -----------------------------------------------------------------
        if (isAliasType(n->type)) {
            ImGui::Separator();
            ImGui::TextDisabled("Referenciar nodo");
            const int curTid  = static_cast<int>(n->params.count("target_node_id")
                                                  ? n->params.at("target_node_id")
                                                  : 0);
            const int curPort = static_cast<int>(n->params.count("target_port")
                                                  ? n->params.at("target_port")
                                                  : 0);
            const NodeInstance* cur = active().findNode(curTid);
            std::string preview;
            if (cur) {
                auto it = cur->stringParams.find("Name");
                std::string nm = (it != cur->stringParams.end() && !it->second.empty())
                    ? it->second
                    : std::string(labelOf(cur->type));
                preview = nm + "  [out " + std::to_string(curPort + 1) + "]";
            } else {
                preview = "(sin asignar)";
            }
            ImGui::SetNextItemWidth(280.f);
            if (ImGui::BeginCombo("##aliasTarget", preview.c_str())) {
                // Opción para borrar.
                if (ImGui::Selectable("(sin asignar)", curTid == 0)) {
                    active().setParam(n->id, "target_node_id", 0.0);
                    active().setParam(n->id, "target_port",    0.0);
                }
                ImGui::Separator();
                // Listar todos los nodos del grafo (excluyéndose a sí mismo
                // y a otros Aliases para evitar cadenas) con sus outputs.
                for (const NodeInstance& other : active().nodes()) {
                    if (other.id == n->id) continue;
                    if (isAliasType(other.type)) continue;
                    const NodeDef& d = defOf(other);
                    if (d.outputPorts <= 0) continue;
                    auto it = other.stringParams.find("Name");
                    std::string nm = (it != other.stringParams.end() && !it->second.empty())
                        ? it->second
                        : std::string(labelOf(other.type));
                    for (int p = 0; p < d.outputPorts; ++p) {
                        std::string label = nm + "  [out " + std::to_string(p + 1) + "]";
                        if (d.outputPortLabels.size() > (size_t)p &&
                            !d.outputPortLabels[p].empty()) {
                            label = nm + "  [" + d.outputPortLabels[p] + "]";
                        }
                        const bool isSel = (other.id == curTid && p == curPort);
                        ImGui::PushID(other.id * 1000 + p);
                        if (ImGui::Selectable(label.c_str(), isSel)) {
                            active().setParam(n->id, "target_node_id",
                                              static_cast<double>(other.id));
                            active().setParam(n->id, "target_port",
                                              static_cast<double>(p));
                        }
                        if (isSel) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }
            if (curTid > 0 && !cur) {
                ImGui::TextColored({0.95f, 0.5f, 0.3f, 1.0f},
                    "✗ El nodo referenciado (id=%d) no existe", curTid);
            } else if (cur) {
                // Diagnóstico: mostrar el ID del target para que el
                // usuario pueda correlacionar "qué nodo es" cuando
                // dos nodos comparten Name o el grafo es grande.
                ImGui::TextDisabled("(id del target: %d)", curTid);
            }
        }

        if (n->type == NodeType::Oscilloscope) {
            ImGui::Separator();
            ImGui::TextDisabled("Canales conectados");
            const NodeDef& d = defOf(*n);
            // Etapa 6I.J: la unidad ya NO se tipea acá — el analyzer
            // la infiere desde la señal upstream.  El panel sólo
            // conserva el label editable (anotación libre del usuario,
            // ej. "θ(t) — posición articular") y muestra la unidad
            // detectada como TextDisabled read-only.
            const auto analysis = scinodes::analyzeUnits(active());
            int countShown = 0;
            for (int port = 0; port < d.inputPorts; ++port) {
                int srcId = -1;
                for (const auto& e : active().edges()) {
                    if (e.toNodeId == n->id &&
                        attrInputPort(e.toAttrId) == port) {
                        srcId = e.fromNodeId; break;
                    }
                }
                if (srcId < 0) continue;
                ++countShown;
                char keyL[32];
                std::snprintf(keyL, sizeof(keyL), "portLabel%d", port);
                std::string curL = n->stringParams.count(keyL)
                    ? n->stringParams.at(keyL) : std::string{};
                char bufL[128];
                std::strncpy(bufL, curL.c_str(), sizeof(bufL) - 1);
                bufL[sizeof(bufL) - 1] = 0;

                // Unidad inferida (read-only).  Sin sufijo si el
                // analyzer no resolvió o el resultado es adimensional×1.
                std::string inferredUnit;
                const int inAttr = n->inputAttrId(port);
                if (analysis.isResolved(inAttr)) {
                    scinodes::Unit u = analysis.unitAt(inAttr);
                    if (!u.isDimensionless() ||
                        std::fabs(u.magnitude - 1.0) > 1e-12) {
                        inferredUnit = u.toCanonicalString();
                    }
                }

                ImGui::PushID(port);
                ImGui::Text("in %d", port + 1);
                ImGui::SameLine(60.f);
                ImGui::SetNextItemWidth(200.f);
                if (ImGui::InputTextWithHint("##lab", "nombre (ej. θ(t))",
                                             bufL, sizeof(bufL)))
                    active().setStringParam(n->id, keyL, bufL);
                ImGui::SameLine();
                if (inferredUnit.empty())
                    ImGui::TextDisabled("[u?]");
                else
                    ImGui::TextDisabled("[%s]", inferredUnit.c_str());
                ImGui::PopID();
            }
            if (countShown == 0) {
                ImGui::TextDisabled("  (conecta un cable a una entrada "
                                    "para etiquetarla)");
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Close", {100, 0}))
            m_openParamPanelNodeId = 0;
    }
    ImGui::End();
    if (!open) m_openParamPanelNodeId = 0;
}
void NodeCanvas::drawRenamePopup() {
    if (m_renameNodeId == 0) return;

    const NodeInstance* node = active().findNode(m_renameNodeId);
    const bool isSG = node && isSubGraphContainer(node->type);

    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##NodeMetaEdit")) {
        ImGui::TextDisabled(" Editar metadatos del nodo");
        ImGui::Separator();

        // Etapa 6I.T: campo Name editable para TODOS los nodos.  Vacío
        // ⇒ se muestra el label del registry (el comportamiento previo).
        // Con texto ⇒ pisa al label.
        ImGui::TextDisabled("Nombre  (vacío = label del tipo)");
        if (m_renameFocusPending) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocusPending = false;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##rn", m_renameBuf, sizeof(m_renameBuf));
        ImGui::Spacing();

        ImGui::TextDisabled("Comentario  (Ctrl+hover sobre el nodo para verlo)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##cmt", m_commentBuf, sizeof(m_commentBuf),
                                  ImVec2(-1, 90));
        ImGui::Spacing();

        const bool apply  = ImGui::Button("Apply");
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");
        if (apply) {
            recordSnapshot(m_graph.snapshot());
            // Vacío = borrar el override → fallback al label del registry.
            if (m_renameBuf[0] == '\0')
                active().setStringParam(m_renameNodeId, "Name", "");
            else
                active().setStringParam(m_renameNodeId, "Name", m_renameBuf);
            active().setComment(m_renameNodeId, m_commentBuf);
            m_renameNodeId = 0;
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            m_renameNodeId = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        // El popup fue cerrado (Esc o click fuera): limpiar estado.
        m_renameNodeId = 0;
    }
    (void)isSG;
}
