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

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <unordered_set>
#include <vector>


// ---------------------------------------------------------------------------
// Estado interno del canvas que no depende del renderer.  El renderer
// (ImNodesRenderer) se inicializa en AppWindow antes de inyectarlo aquí
// vía setRenderer(); este init() es solo para futuras inicializaciones
// específicas del NodeCanvas (hoy, ninguna).
void NodeCanvas::init() {
    // Por ahora no hay estado propio que inicializar.  El renderer
    // ya fue inicializado por AppWindow.
}

// ---------------------------------------------------------------------------
void NodeCanvas::drawContent() {
    // Drain a pending custom-node load dialog (started from the add popup).
    if (!m_loadCustomDialog.isOpen()) {
        std::string p = m_loadCustomDialog.take();
        if (!p.empty()) {
            std::string err;
            bool ok = scinodes::customNodes()
                          .loadFromFile(p, &err);
            m_customLoadStatus = ok
                ? ("Loaded custom node from " + p)
                : ("Load failed: " + err);
        }
    }

    // Drain a pending param CSV dialog (Import / Export from the param panel).
    if (m_paramCsvAction != ParamCsvAction::None && !m_paramCsvDialog.isOpen()) {
        std::string path = m_paramCsvDialog.take();
        if (!path.empty()) {
            // Append .csv if the user omitted an extension.
            auto dot = path.rfind('.');
            auto sep = path.find_last_of("/\\");
            if (dot == std::string::npos ||
                (sep != std::string::npos && dot < sep))
                path += ".csv";

            const NodeInstance* n = active().findNode(m_paramCsvNodeId);
            if (!n) {
                m_paramCsvStatus = "Param CSV failed: node no longer exists.";
            } else if (m_paramCsvAction == ParamCsvAction::Export) {
                std::string err;
                char label[80];
                std::snprintf(label, sizeof(label), "%s #%d",
                              defOf(*n).label.c_str(), n->id);
                bool ok = scinodes::writeNodeParamsCsv(path, *n, label, &err);
                m_paramCsvStatus = ok
                    ? ("Exported to " + path)
                    : ("Export failed: " + err);
            } else {
                // Import — snapshot for undo, mutate a temp copy, write back.
                NodeInstance tmp = *n;
                std::string err;
                std::vector<std::string> warns;
                bool ok = scinodes::readNodeParamsCsv(path, tmp, &err, &warns);
                if (ok) {
                    recordSnapshot(m_graph.snapshot());
                    for (const auto& [name, value] : tmp.params)
                        active().setParam(n->id, name, value);
                    if (m_paramCallback) {
                        // Re-emit each param so the live bridge updates too.
                        const auto& def = defOf(*n);
                        for (int i = 0; i < (int)def.params.size(); ++i) {
                            auto it = tmp.params.find(def.params[i].name);
                            if (it != tmp.params.end())
                                m_paramCallback(pathFor(n->id), i, it->second);
                        }
                    }
                    m_paramCsvStatus = "Imported " + path
                        + (warns.empty() ? "" :
                           " (" + std::to_string(warns.size()) +
                           " warnings)");
                } else {
                    m_paramCsvStatus = "Import failed: " + err;
                }
            }
        }
        m_paramCsvAction = ParamCsvAction::None;
        m_paramCsvNodeId = 0;
    }

    // Drain del file dialog para asignar asset 3D a un nodo Device.
    if (m_assetDialogNodeId != 0 && !m_assetDialog.isOpen()) {
        std::string path = m_assetDialog.take();
        if (!path.empty()) {
            const int nodeId = m_assetDialogNodeId;
            active().setAssetPath(nodeId, path);
            reloadAssetFor(nodeId);

            // Si el asset cargado no satisface el contrato Y no hay
            // sidecar todavía, abrir el panel de mapping
            // automáticamente — el usuario probablemente quiere
            // mapear nodo-por-nodo a mano, no re-exportar.
            const scinodes::DeviceAsset* asset =
                m_assetService ? m_assetService->find(nodeId) : nullptr;
            if (asset && !asset->valid() &&
                !scinodes::app::AssetService::sidecarExists(path)) {
                openMappingPanelFor(nodeId);
            }
        }
        m_assetDialogNodeId = 0;
    }

    // Render del mapping panel — un solo frame por ciclo.  Cuando el
    // usuario confirma, persistimos el sidecar y recargamos el asset
    // para que el nuevo binding tome efecto.
    if (m_mappingPanel.drawFrame()) {
        std::string err;
        const auto& map = m_mappingPanel.result();
        if (map.saveToFile(m_mappingPanel.sidecarPath(), &err)) {
            if (m_mappingNodeId != 0) reloadAssetFor(m_mappingNodeId);
        } else {
            m_errorMsg   = "No se pudo guardar el mapping: " + err;
            m_errorTimer = 5.0f;
        }
        m_mappingNodeId = 0;
    }

    // Focus-follows-mouse estilo Blender.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive()) {
        ImGui::SetWindowFocus();
    }

    handleZoom();
    if (!m_readOnly) handleUndoRedo();

    // Read-only banner
    if (m_readOnly) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 170, 60, 255));
        ImGui::TextUnformatted("  READ-ONLY — loaded graph has grammar violations.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // Shift+A → add-node popup at cursor (browse mode, no auto-connect).
    if (!m_readOnly &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        openAddPopup(ImGui::GetMousePos());
    }
    // Shift+B → find-node popup (etapa 6M).
    handleFindTrigger();
    // C / E sobre el nodo seleccionado: centrar (pan-only) / encuadrar.
    handleViewKeys();

    drawAddPopup();
    drawFindPopup();
    drawRenamePopup();
    showErrorTooltip();

    // Breadcrumb (visible solo cuando hay sub-niveles activos).
    if (!m_canvasStack.empty()) drawBreadcrumb();

    // Activar el contexto del nivel actual.  Cada subgrafo tiene su
    // propio espacio de ids/posiciones — la cache vive ahora dentro
    // del renderer (ImNodesRenderer mantiene el unordered_map por
    // path-key); aquí solo le decimos qué contexto abrir.
    ImGui::SetWindowFontScale(m_zoom);
    m_renderer->beginCanvas(canvasPathKey());

    // Apply any positions queued by a recent load — must happen inside the
    // editor scope so imnodes treats them as authoritative for this frame.
    if (m_applyPositionsPending) {
        applyPositionsToImnodes();
        m_applyPositionsPending = false;
    }

    // Etapa 6M: si el find popup eligió un nodo, seleccionarlo en el
    // canvas activo (ya navegamos al SubGraph correcto en focusNode()).
    // 6M.c — además paneamos sobre el nodo (pan-only, zoom preservado)
    // para que SIEMPRE haya feedback visual de que el find encontró
    // algo.  El user puede pulsar E para encuadre con zoom si quiere.
    if (m_pendingSelectNode != 0) {
        m_renderer->clearNodeSelection();
        m_renderer->selectNode(m_pendingSelectNode);
        if (m_pendingCenterNode) {
            if (const NodeInstance* fn = active().findNode(m_pendingSelectNode)) {
                // Etapa 6M.d: la posición vive en el cache del renderer
                // (m_activeState->positions), no en NodeInstance::position.
                // Esta última está stale tras el primer drag del user.
                using Space = scinodes::ui::INodeRenderer::CoordSpace;
                const auto pos = m_renderer->getNodePosition(
                    m_pendingSelectNode, Space::Editor);
                const auto dims = scinodes::ui::computeNodeDimensions(*fn, &active());
                m_renderer->centerOn({ pos.x + dims.w * 0.5f,
                                        pos.y + dims.h * 0.5f });
            }
            m_pendingCenterNode = false;
        }
        m_pendingSelectNode = 0;
    }

    if (m_readOnly) ImGui::BeginDisabled();
    NodeGraph& g = active();
    for (const auto& n : g.nodes())
        drawNode(n);

    drawEdges();

    m_renderer->endCanvas();
    if (m_readOnly) ImGui::EndDisabled();
    ImGui::SetWindowFontScale(1.0f);


    // Tooltip de comentario via Ctrl+hover.  Solo aparece cuando el
    // usuario tiene Ctrl presionado y el cursor sobre un nodo con
    // comentario no-vacío.  Sin Ctrl el tooltip no se dispara — esa
    // es la regla unificada de la UI ("Ctrl para más info").
    if (ImGui::GetIO().KeyCtrl) {
        int hoveredNodeId = 0;
        if (m_renderer->isNodeHovered(hoveredNodeId)) {
            if (const NodeInstance* hn = active().findNode(hoveredNodeId)) {
                if (!hn->comment.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(360.f);
                    ImGui::TextUnformatted(hn->comment.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
        }
    }

    // El overlay con los atajos vive ahora dentro del native renderer
    // (NativeNodeRenderer::endCanvas), donde tiene acceso al child
    // window correcto del canvas.  Antes se dibujaba aquí pero quedaba
    // fuera del clip rect tras el refactor del renderer.

    if (!m_readOnly) {
        handleLinkCreated();
        handleLinkDropped();
        handleEdgeContextMenu();
        handleDeletion();
        handleCopyPaste();
        handleEncapsulate();
        handleRename();
        handleAutoLayout();
    }
    handleParamPanelTrigger();

    // The parameter panel renders as its own top-level ImGui window
    // outside of the canvas Begin/End — that way the user can move it
    // independently of the canvas and it does not get clipped.
    drawParamPanel();
}

// ---------------------------------------------------------------------------
// Double-click on a node → open the parameter panel for it.
// ---------------------------------------------------------------------------
void NodeCanvas::handleParamPanelTrigger() {
    int hoveredId = 0;
    if (!m_renderer->isNodeHovered(hoveredId)) return;
    if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;
    if (hoveredId <= 0) return;
    // Si el doble-click recayó sobre un widget activo (DragFloat
    // entrando a modo text-input, InputText, …), ese widget lo consume
    // y el panel flotante NO debe abrirse — sería un editor encima del
    // editor que el usuario quería usar.
    if (ImGui::IsAnyItemActive()) return;
    // Doble-click sobre un SubGraph: navegar al subgrafo en vez de
    // abrir el panel de parámetros.  El resto de tipos abren panel.
    if (const NodeInstance* n = active().findNode(hoveredId)) {
        if (isSubGraphContainer(n->type)) {
            enterSubGraph(hoveredId);
            return;
        }
    }
    m_openParamPanelNodeId = hoveredId;
    m_paramPanelPos        = ImGui::GetMousePos();
}




// ---------------------------------------------------------------------------
// handleLinkCreated — called after EndNodeEditor
// ---------------------------------------------------------------------------
void NodeCanvas::handleLinkCreated() {
    scinodes::ui::LinkCreatedEvent ev;
    if (!m_renderer->pollLinkCreated(ev)) return;
    int fromAttr = ev.fromAttrId, toAttr = ev.toAttrId;

    auto before = m_graph.snapshot();
    auto err    = active().tryAddEdge(fromAttr, toAttr);

    if (err) {
        // Show error tooltip and do NOT record undo
        m_errorMsg   = "[" + err->rule + "]  " + err->message;
        m_errorTimer = 3.5f;
    } else {
        recordSnapshot(before);   // record "before" for undo
        bumpDirty();
    }
}

// ---------------------------------------------------------------------------
// handleDeletion — Delete / Backspace removes selected nodes and edges
// ---------------------------------------------------------------------------
void NodeCanvas::handleDeletion() {
    // Don't steal Delete/Backspace while a parameter field has keyboard focus.
    if (ImGui::IsAnyItemActive()) return;

    if (!ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        !ImGui::IsKeyPressed(ImGuiKey_Backspace))
        return;

    // Sim activa con selección presente: bloqueo + mensaje claro al
    // usuario.  Una desconexión cambia la identidad del sistema
    // simulado, así que exigimos detener la sim primero.  Mostramos
    // el mensaje solo si HABÍA algo seleccionado para no spamear con
    // toques accidentales de Delete sobre el vacío.
    if (m_simActive) {
        std::vector<int> selN, selL;
        m_renderer->getSelectedNodes(selN);
        m_renderer->getSelectedLinks(selL);
        if (!selN.empty() || !selL.empty()) {
            m_errorMsg   = "Detén la simulación para eliminar — esto "
                           "cambia el sistema que está siendo simulado.";
            m_errorTimer = 3.5f;
        }
        return;
    }

    bool changed = false;

    // Selected edges
    {
        std::vector<int> sel;
        m_renderer->getSelectedLinks(sel);
        if (!sel.empty()) {
            auto before = m_graph.snapshot();
            for (int id : sel) active().removeEdge(id);
            recordSnapshot(before);
            changed = true;
        }
    }

    // Selected nodes (also removes their edges)
    {
        std::vector<int> sel;
        m_renderer->getSelectedNodes(sel);
        if (!sel.empty()) {
            auto before = m_graph.snapshot();
            for (int id : sel) active().removeNode(id);
            recordSnapshot(before);
            changed = true;
        }
    }

    if (changed) bumpDirty();
}

// ---------------------------------------------------------------------------
// handleUndoRedo
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// handleCopyPaste — Ctrl+C captures the current selection; Ctrl+V duplicates
// the clipboard at the cursor preserving relative layout.  Only edges that
// connect two *selected* nodes are captured; edges crossing the selection
// boundary are dropped on the floor.  Each paste creates fresh node ids
// (via NodeGraph::addNode) so the new copy is fully independent.
// ---------------------------------------------------------------------------
void NodeCanvas::handleCopyPaste() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl)               return;
    if (ImGui::IsAnyItemActive())  return;   // a text input has focus
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelectionToClipboard();
    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteClipboard();
}

void NodeCanvas::copySelectionToClipboard() {
    std::vector<int> sel;
    m_renderer->getSelectedNodes(sel);
    if (sel.empty()) return;
    std::unordered_set<int> selSet(sel.begin(), sel.end());

    m_clipNodes.clear();
    m_clipEdges.clear();

    float minX =  std::numeric_limits<float>::infinity();
    float minY =  std::numeric_limits<float>::infinity();

    NodeGraph& g = active();
    for (int id : sel) {
        const NodeInstance* node = g.findNode(id);
        if (!node) continue;
        auto rp = m_renderer->getNodePosition(id);
        ImVec2 pos{ rp.x, rp.y };
        ClipboardEntry ent;
        ent.node = *node;
        ent.pos  = pos;
        // SubGraph: clonar el contenido del child para que el paste
        // re-instale la topología interna intacta.  Sin esto el paste
        // crearía un SubGraph "vacío" sin grafo hijo y la simulación o
        // navegación posterior podrían derefenciar punteros nulos.
        if (isSubGraphContainer(node->type)) {
            if (const NodeGraph* child = g.subGraphOf(id)) {
                ent.childGraph = std::make_shared<NodeGraph>(*child);
                // Capturar posiciones del EditorContext del child.
                // imnodes guarda las posiciones por contexto/nodo-id;
                // al pegar creamos un nuevo SubGraph con un nuevo
                // context-key, así que tenemos que llevar las
                // posiciones explícitamente.  Los node-ids dentro del
                // child se preservan al instalar, así que la misma
                // tabla {id → pos} se reutiliza tal cual en el paste.
                const std::string childKey = canvasPathKey() +
                                             std::to_string(id) + "/";
                m_renderer->pushCanvas(childKey);
                for (const NodeInstance& cn : child->nodes()) {
                    auto p = m_renderer->getNodePosition(cn.id);
                    ent.internalPositions[cn.id] = ImVec2{ p.x, p.y };
                }
                m_renderer->popCanvas();
            }
        }
        m_clipNodes.push_back(std::move(ent));
        minX = std::min(minX, pos.x);
        minY = std::min(minY, pos.y);
    }
    m_clipAnchor = { minX, minY };

    // Sólo capturamos aristas internas — ambos extremos seleccionados.
    for (const Edge& e : active().edges()) {
        if (selSet.count(e.fromNodeId) && selSet.count(e.toNodeId))
            m_clipEdges.push_back(e);
    }
}

void NodeCanvas::pasteClipboard() {
    if (m_clipNodes.empty()) return;

    recordSnapshot(m_graph.snapshot());
    bumpDirty();

    // Offset desde el anchor del clipboard hasta el cursor actual.
    // Pequeño nudge para que el paste no quede exactamente encima de la
    // selección original cuando se pega en el mismo grafo (Ctrl+C, Ctrl+V).
    const ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 offset = { mouse.x - m_clipAnchor.x,
                      mouse.y - m_clipAnchor.y };
    if (std::fabs(offset.x) < 6.0f && std::fabs(offset.y) < 6.0f) {
        offset.x += 24.0f; offset.y += 24.0f;
    }

    // Mapeo oldNodeId → newNodeId para reescribir los attrIds de las aristas.
    std::unordered_map<int, int> idMap;
    std::vector<int> newIds;
    newIds.reserve(m_clipNodes.size());

    for (const ClipboardEntry& ent : m_clipNodes) {
        int newId = 0;
        if (ent.node.type == NodeType::Custom) {
            newId = active().addCustomNode(ent.node.customType);
        } else if (isSubGraphContainer(ent.node.type)) {
            // Crear el SubGraph correctamente (con stubs default que luego
            // serán reemplazados al instalar el child del clipboard).
            newId = active().addSubGraphNode();
            if (ent.childGraph) {
                NodeGraph childCopy = *ent.childGraph;
                active().installSubGraph(newId, std::move(childCopy));
                active().recomputeSubGraphPorts(newId);

                // Sembrar las posiciones internas en el EditorContext
                // del nuevo child.  Los node-ids del childGraph se
                // preservaron al instalar, así que las posiciones
                // capturadas en copy aplican directamente.
                if (!ent.internalPositions.empty()) {
                    const std::string childKey = canvasPathKey() +
                                                 std::to_string(newId) + "/";
                    m_renderer->pushCanvas(childKey);
                    for (const auto& [cid, cpos] : ent.internalPositions) {
                        m_renderer->setNodePosition(cid, { cpos.x, cpos.y });
                    }
                    m_renderer->popCanvas();
                }
            }
        } else {
            newId = active().addNode(ent.node.type);
        }
        if (newId <= 0) continue;
        idMap[ent.node.id] = newId;
        newIds.push_back(newId);

        // Copiar todos los parámetros (numéricos + string + assetPath).
        for (const auto& [k, v] : ent.node.params)
            active().setParam(newId, k, v);
        for (const auto& [k, v] : ent.node.stringParams)
            active().setStringParam(newId, k, v);
        if (!ent.node.assetPath.empty())
            active().setAssetPath(newId, ent.node.assetPath);

        // Posición: misma posición relativa que en el clipboard + offset.
        m_renderer->setNodePosition(newId,
            { ent.pos.x + offset.x, ent.pos.y + offset.y });
    }

    // Re-cablear las aristas internas remapeando los attrIds.
    for (const Edge& e : m_clipEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        active().tryAddEdge(attrRemap(e.fromAttrId, fIt->second),
                            attrRemap(e.toAttrId,   tIt->second));
    }

    // Selección actualizada: los nodos recién pegados quedan seleccionados,
    // así un paste seguido de un drag mueve todo el bloque junto.
    m_renderer->clearNodeSelection();
    for (int id : newIds) m_renderer->selectNode(id);
}

// ---------------------------------------------------------------------------
// Ctrl+G — encapsulate the selection into a SubGraph.  Edges that cross the
// selection boundary become external ports of the new SubGraph (materialized
// internally as `SubGraphInput`/`SubGraphOutput` stubs).
// ---------------------------------------------------------------------------
void NodeCanvas::handleEncapsulate() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_G, false)) return;
    encapsulateSelection();
}

void NodeCanvas::encapsulateSelection() {
    std::vector<int> sel;
    m_renderer->getSelectedNodes(sel);
    if (sel.empty()) return;
    const int n = static_cast<int>(sel.size());

    // Centro de masa de la selección — la lógica pura no conoce posiciones,
    // así que lo calculamos aquí y se lo aplicamos al nodo resultante.
    ImVec2 acc{0, 0};
    for (int id : sel) {
        auto p = m_renderer->getNodePosition(id);
        acc = { acc.x + p.x, acc.y + p.y };
    }
    const ImVec2 center{ acc.x / n, acc.y / n };

    // Capturar posiciones antes de mutar — las usamos para sembrar el
    // contexto del child y preservar el layout interno.
    std::unordered_map<int, ImVec2> oldPos;
    for (int id : sel) {
        auto p = m_renderer->getNodePosition(id);
        oldPos[id] = ImVec2{ p.x, p.y };
    }

    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    auto res = active().encapsulateByIds(sel);
    if (res.sgId == 0) {
        m_errorMsg   = "[encapsulate] selección inválida (¿stubs?).";
        m_errorTimer = 3.5f;
        return;
    }
    m_renderer->setNodePosition(res.sgId, { center.x, center.y });
    m_renderer->clearNodeSelection();
    m_renderer->selectNode(res.sgId);

    // Encapsular es un refactor estructural — la jerarquía visible
    // cambia pero el grafo aplanado (= las dinámicas que ve el solver)
    // queda equivalente.  Marcamos el flag para que AppWindow le pida
    // a SimController refrescar la baseline; sin eso, el siguiente
    // Resume vería los nodos viejos como "removidos" del top-level y
    // dispararía el modal destructivo de forma falsa.
    m_refactorJustHappened = true;

    // Sembrar las posiciones internas del SubGraph antes de que el
    // usuario entre.  Como el child vive en su propio EditorContext,
    // cambiamos a él temporalmente, fijamos las posiciones via
    // SetNodeScreenSpacePos (que opera en el contexto activo) y
    // restauramos el contexto del padre.  Las posiciones relativas
    // entre los nodos seleccionados se preservan; los stubs
    // SubGraphInput / SubGraphOutput, que vienen sin posición,
    // se colocan en dos columnas a la izquierda y derecha del
    // bbox de la selección movida (un stub por fila, ordenados
    // por su parámetro "Port").  Si el usuario quiere reorganizar
    // a mano lo puede hacer; el objetivo es solo que al entrar
    // por primera vez al SubGraph todo se vea legible, no que
    // todos los stubs queden amontonados en el origen.
    {
        const std::string childKey = canvasPathKey() +
                                     std::to_string(res.sgId) + "/";
        m_renderer->pushCanvas(childKey);

        // 1) Posiciones de los nodos movidos (preservan layout relativo).
        constexpr float kInf = std::numeric_limits<float>::infinity();
        float minX =  kInf, maxX = -kInf;
        float minY =  kInf, maxY = -kInf;
        for (const auto& [oldId, newId] : res.idMap) {
            auto it = oldPos.find(oldId);
            if (it == oldPos.end()) continue;
            m_renderer->setNodePosition(newId, { it->second.x, it->second.y });
            minX = std::min(minX, it->second.x);
            maxX = std::max(maxX, it->second.x);
            minY = std::min(minY, it->second.y);
            maxY = std::max(maxY, it->second.y);
        }
        // Si no había posiciones válidas (caso degenerado), centrar
        // el layout en (200,200) — sirve como punto de partida.
        if (minX > maxX) { minX = 200.f; maxX = 600.f; minY = 200.f; maxY = 400.f; }

        // 2) Stubs en dos columnas.  Sort por Port para layout estable.
        std::vector<std::pair<int,int>> inStubs;   // (port, nodeId)
        std::vector<std::pair<int,int>> outStubs;
        const NodeGraph* child = active().subGraphOf(res.sgId);
        if (child) {
            for (const NodeInstance& n : child->nodes()) {
                int port = 0;
                auto pit = n.params.find("Port");
                if (pit != n.params.end()) port = static_cast<int>(pit->second);
                if (n.type == NodeType::SubGraphInput)
                    inStubs.emplace_back(port, n.id);
                else if (n.type == NodeType::SubGraphOutput)
                    outStubs.emplace_back(port, n.id);
            }
        }
        std::sort(inStubs.begin(),  inStubs.end());
        std::sort(outStubs.begin(), outStubs.end());

        constexpr float kStubMargin  = 220.0f;   // separación del bbox
        constexpr float kStubSpacing = 80.0f;    // entre stubs verticalmente
        const float midY = 0.5f * (minY + maxY);
        // Centrar verticalmente la columna de stubs respecto al midY del bbox.
        auto columnTopY = [&](size_t count) {
            return midY - 0.5f * static_cast<float>(count - 1) * kStubSpacing;
        };

        if (!inStubs.empty()) {
            const float y0 = columnTopY(inStubs.size());
            for (size_t k = 0; k < inStubs.size(); ++k) {
                m_renderer->setNodePosition(
                    inStubs[k].second,
                    { minX - kStubMargin, y0 + k * kStubSpacing });
            }
        }
        if (!outStubs.empty()) {
            const float y0 = columnTopY(outStubs.size());
            for (size_t k = 0; k < outStubs.size(); ++k) {
                m_renderer->setNodePosition(
                    outStubs[k].second,
                    { maxX + kStubMargin, y0 + k * kStubSpacing });
            }
        }

        m_renderer->popCanvas();
    }
}


// ---------------------------------------------------------------------------
// SubGraph navigation
// ---------------------------------------------------------------------------
NodeGraph& NodeCanvas::active() {
    NodeGraph* g = &m_graph;
    // Recorre el stack por índice (NO por range-for) para poder recortarlo
    // de forma segura si un ancestro desaparece tras un undo.
    for (size_t i = 0; i < m_canvasStack.size(); ++i) {
        NodeGraph* child = g->subGraphOf(m_canvasStack[i]);
        if (!child) {
            // Recortar al ancestro válido más profundo en vez de saltar
            // al top-level — preserva la mayor parte del contexto del
            // usuario cuando sólo un sub-sub-grafo dejó de existir.
            m_canvasStack.resize(i);
            return *g;
        }
        g = child;
    }
    return *g;
}
const NodeGraph& NodeCanvas::active() const {
    const NodeGraph* g = &m_graph;
    for (size_t i = 0; i < m_canvasStack.size(); ++i) {
        const NodeGraph* child = g->subGraphOf(m_canvasStack[i]);
        if (!child) return *g;
        g = child;
    }
    return *g;
}

std::string NodeCanvas::canvasPathKey() const {
    std::string s = "/";
    for (int id : m_canvasStack) {
        s += std::to_string(id);
        s += '/';
    }
    return s;
}

void NodeCanvas::enterSubGraph(int subGraphId) {
    if (!active().subGraphOf(subGraphId)) return;
    m_canvasStack.push_back(subGraphId);
    m_applyPositionsPending = false;   // child context has its own positions

    // Auto-layout heurístico: si al entrar al child detectamos que las
    // posiciones de sus nodos están "amontonadas" (bbox total más chica
    // que un umbral pequeño), aplicamos auto-layout.  Esto cubre dos
    // casos: (a) un SubGraph recién creado vacío y luego poblado con
    // nodos sin layout y (b) cargas de .scn legacy sin posiciones
    // internas.  Los casos con layout válido (encapsulate, paste, .scn
    // moderno) producen un bbox grande y se saltan el auto-layout.
    const std::string childKey = canvasPathKey();
    m_renderer->pushCanvas(childKey);
    const NodeGraph& child = active();
    if (!child.nodes().empty()) {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        float minX = kInf, maxX = -kInf, minY = kInf, maxY = -kInf;
        for (const NodeInstance& n : child.nodes()) {
            auto p = m_renderer->getNodePosition(n.id);
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        const float spreadX = maxX - minX;
        const float spreadY = maxY - minY;
        if (spreadX < 50.0f && spreadY < 50.0f) {
            // Nodos amontonados — aplicar auto-layout.  Opera sobre
            // active() = child (estamos dentro del contexto ya).
            applyAutoLayout();
        }
    }
    m_renderer->popCanvas();
}

void NodeCanvas::exitToLevel(int depth) {
    if (depth < 0) depth = 0;
    while (static_cast<int>(m_canvasStack.size()) > depth)
        m_canvasStack.pop_back();
}


// F2 sobre selección — abre el popup de edición sobre cualquier nodo
// seleccionado.  El popup tiene dos campos:
//   - "Name" (visible solo si el nodo es un SubGraph) edita
//      stringParams["Name"].
//   - "Comment" (siempre visible) edita n.comment — texto libre para
//      anotar el "por qué" detrás del nodo.
// Multi-selección se ignora (no tiene sentido un comentario compartido).
void NodeCanvas::handleRename() {
    if (m_renameNodeId != 0) return;   // popup ya abierto
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_F2, false)) return;

    std::vector<int> ids;
    m_renderer->getSelectedNodes(ids);
    if (ids.size() != 1) return;
    int id = ids[0];
    if (id <= 0) return;
    const NodeInstance* node = active().findNode(id);
    if (!node) return;

    m_renameNodeId       = id;
    m_renameFocusPending = true;

    // Cargar Name actual — etapa 6I.T: todos los nodos pueden tener
    // nombre custom (antes era exclusivo de SubGraphs).  Permite
    // distinguir "PID velocidad" de "PID posición", y habilita el
    // futuro nodo Alias que referencia por nombre.
    {
        auto it = node->stringParams.find("Name");
        const std::string cur = (it != node->stringParams.end()) ? it->second
                                                                  : std::string();
        std::strncpy(m_renameBuf, cur.c_str(), sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }
    // Cargar comentario actual.
    std::strncpy(m_commentBuf, node->comment.c_str(), sizeof(m_commentBuf) - 1);
    m_commentBuf[sizeof(m_commentBuf) - 1] = '\0';
    ImGui::OpenPopup("##NodeMetaEdit");
}


// ---------------------------------------------------------------------------
// Ctrl+L — Auto-layout BFS-layered del grafo activo.
// ---------------------------------------------------------------------------
void NodeCanvas::handleAutoLayout() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;
    if (ImGui::IsAnyItemActive()) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_L, false)) return;
    applyAutoLayout();
}

void NodeCanvas::applyAutoLayout() {
    NodeGraph& g = active();
    if (g.nodes().empty()) return;

    // Delegamos al `Canvas` propio (src/ui/canvas/Canvas.cpp): construimos
    // un Canvas efímero sobre el grafo activo, llamamos autoLayout (que
    // devuelve posiciones en MODEL space, no en screen-space) y se las
    // pasamos al renderer en Editor space para que se persistan
    // independientes del pan/zoom actual.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    scinodes::ui::Canvas canvas(g);
    canvas.autoLayout();
    constexpr float kInf = 1e9f;
    float minX = kInf, minY = kInf, maxX = -kInf, maxY = -kInf;
    for (const NodeInstance& n : g.nodes()) {
        const auto pos = canvas.positionOf(n.id);
        m_renderer->setNodePosition(n.id, { pos.x, pos.y }, Space::Editor);
        const auto dims = scinodes::ui::computeNodeDimensions(n, &g);
        if (pos.x < minX) minX = pos.x;
        if (pos.y < minY) minY = pos.y;
        if (pos.x + dims.w > maxX) maxX = pos.x + dims.w;
        if (pos.y + dims.h > maxY) maxY = pos.y + dims.h;
    }
    // Encuadrar la vista al bbox real (incluyendo dimensiones de cada
    // nodo, no aproximaciones).  Sin esto los nodos anchos (p. ej.
    // Oscilloscope con port-label custom largo) quedan parcialmente
    // fuera del viewport.
    if (minX <= maxX)
        m_renderer->frameToBox({minX, minY}, {maxX, maxY}, 0.f, 0.f);

    recordSnapshot(m_graph.snapshot());
    bumpDirty();
}

// ---------------------------------------------------------------------------
// Find popup (etapa 6M) — search/focus por nombre, recursivo por SubGraphs.
// ---------------------------------------------------------------------------
std::string NodeCanvas::displayNameOf(const NodeInstance& n) const {
    // Nombre custom (F2) si está presente; si no, el label traducido del
    // tipo + el id para distinguir entre instancias del mismo tipo.
    if (auto it = n.stringParams.find("Name");
        it != n.stringParams.end() && !it->second.empty())
        return it->second;
    const NodeDef& def = defOf(n);
    const std::string typeLabel = scinodes::trOr(
        std::string("node.") + typeName(n.type) + ".label",
        def.label);
    return typeLabel + " #" + std::to_string(n.id);
}

static std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

void NodeCanvas::searchInto(std::vector<NodeSearchHit>& out,
                            const NodeGraph& g,
                            const std::vector<int>& path,
                            const std::string& queryLower) const
{
    for (const NodeInstance& n : g.nodes()) {
        // Stubs internos del SubGraph no son nodos navegables.
        if (isSubGraphStub(n.type)) continue;
        const std::string name = displayNameOf(n);
        if (queryLower.empty() ||
            toLowerAscii(name).find(queryLower) != std::string::npos)
        {
            NodeSearchHit hit;
            hit.canvasPath  = path;
            hit.nodeId      = n.id;
            hit.displayName = name;
            // Breadcrumb: "Top › SG1 › SG2 › <name>".  Cada SG se identifica
            // por su nombre del stringParam (si renombrado) o "SubGrafo #id".
            std::string crumb = scinodes::tr("find.breadcrumb_root");
            // Resolver cada step del path consultando el grafo padre.
            const NodeGraph* cursor = &m_graph;
            for (int sgId : path) {
                const NodeInstance* sgNode = cursor->findNode(sgId);
                if (sgNode) crumb += " › " + displayNameOf(*sgNode);
                cursor = cursor->subGraphOf(sgId);
                if (!cursor) break;
            }
            hit.breadcrumb = crumb;
            out.push_back(std::move(hit));
        }
        // Recurse hacia SubGraphs hijos.
        if (isSubGraphContainer(n.type)) {
            if (const NodeGraph* child = g.subGraphOf(n.id)) {
                std::vector<int> nextPath = path;
                nextPath.push_back(n.id);
                searchInto(out, *child, nextPath, queryLower);
            }
        }
    }
}

std::vector<NodeCanvas::NodeSearchHit>
NodeCanvas::searchNodes(const std::string& query) const {
    std::vector<NodeSearchHit> out;
    const std::string q = toLowerAscii(query);
    searchInto(out, m_graph, {}, q);
    return out;
}

void NodeCanvas::focusNode(const NodeSearchHit& hit) {
    // 1. Navegar al subgrafo donde vive.
    exitToLevel(0);
    for (int sgId : hit.canvasPath) enterSubGraph(sgId);
    // 2. Diferir SELECCIÓN + auto-PANEO al próximo frame (cuando
    //    beginCanvas() del contexto destino ya esté activo).
    //    Etapa 6M.c — el find SIEMPRE panea para mostrar el nodo, sin
    //    tocar el zoom.  Sin esto, encontrar un nodo offscreen se
    //    siente como "no se hizo nada".  El user puede pulsar E
    //    después si quiere encuadre completo (zoom + pan).
    m_pendingSelectNode = hit.nodeId;
    m_pendingCenterNode = true;
    m_findOpen = false;
}

void NodeCanvas::handleViewKeys() {
    // C: pan-only center sobre el nodo seleccionado.
    // E: frameToBox (zoom + pan) sobre el nodo seleccionado.
    // Ambas requieren: canvas hovered + ningún ItemActive (sino tipear
    // "c" en un InputText dispararía centro).
    if (m_readOnly) return;
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) return;
    if (ImGui::IsAnyItemActive()) return;
    const bool pressC = ImGui::IsKeyPressed(ImGuiKey_C, false);
    const bool pressE = ImGui::IsKeyPressed(ImGuiKey_E, false);
    if (!pressC && !pressE) return;
    if (!m_renderer) return;
    std::vector<int> sel;
    m_renderer->getSelectedNodes(sel);
    if (sel.empty()) return;
    // Tomamos el primero — si hay varios el bbox sería arbitrario;
    // centrar/encuadrar sobre uno es comportamiento intuitivo del find.
    const NodeInstance* n = active().findNode(sel.front());
    if (!n) return;
    const auto dims = scinodes::ui::computeNodeDimensions(*n, &active());
    // Etapa 6M.d — la posición la mantiene el renderer (el cache se
    // actualiza con cada drag); NodeInstance::position está stale.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    const auto pos = m_renderer->getNodePosition(sel.front(), Space::Editor);
    const scinodes::ui::CanvasPos lo{ pos.x, pos.y };
    const scinodes::ui::CanvasPos hi{ pos.x + dims.w, pos.y + dims.h };
    if (pressC) {
        m_renderer->centerOn({ 0.5f * (lo.x + hi.x),
                                0.5f * (lo.y + hi.y) });
    } else /*pressE*/ {
        // Cap zoom a 1.5× para "encuadre cómodo de un solo nodo".  Sin
        // este cap, un nodo chico en un panel ancho explotaba a 3×
        // (kZoomMax) llenando todo y ocultando el contexto del grafo.
        m_renderer->frameToBox(lo, hi,
                               /*viewportW=*/0.f, /*viewportH=*/0.f,
                               /*maxZoom=*/1.5f);
    }
}

void NodeCanvas::handleFindTrigger() {
    // Shift+B abre el find popup.  Mismo gating que el add popup.
    if (m_readOnly) return;
    if (m_findOpen) return;
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) return;
    if (!ImGui::GetIO().KeyShift) return;
    if (!ImGui::IsKeyPressed(ImGuiKey_B, false)) return;
    openFindPopup();
}

void NodeCanvas::openFindPopup() {
    m_findOpen           = true;
    m_findBuf[0]         = '\0';
    m_findFocusPending   = true;
    m_popupPos           = ImGui::GetMousePos();
}

void NodeCanvas::handleUndoRedo() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;

    bool undo = ImGui::IsKeyPressed(ImGuiKey_Z);
    bool redo = ImGui::IsKeyPressed(ImGuiKey_Y) ||
                (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift);

    if (undo && !io.KeyShift && m_history.canUndo()) doUndoOrRedo(true);
    if (redo                  && m_history.canRedo()) doUndoOrRedo(false);
}

void NodeCanvas::recordSnapshot(GraphSnapshot snap) {
    // Sync imnodes positions into the snapshot so the inverse op can
    // restore them.  Captures both top-level positions (which is what
    // imnodes knows about right now) — SubGraph contents that aren't
    // currently visible keep whatever positions they had captured by
    // an earlier record.
    syncPositionsFromImnodes();
    snap.positions.clear();
    for (const auto& [id, p] : m_positions)
        snap.positions[id] = { p.x, p.y };
    m_history.record(std::move(snap));
}

bool NodeCanvas::doUndoOrRedo(bool isUndo) {
    syncPositionsFromImnodes();
    GraphSnapshot cur = m_graph.snapshot();
    for (const auto& [id, p] : m_positions)
        cur.positions[id] = { p.x, p.y };

    auto restored = isUndo ? m_history.undo(std::move(cur))
                           : m_history.redo(std::move(cur));
    if (!restored) return false;

    m_graph.restoreSnapshot(*restored);
    bumpDirty();
    m_positions.clear();
    for (const auto& [id, p] : restored->positions)
        m_positions[id] = ScnVec2{ p.first, p.second };
    m_applyPositionsPending = !m_positions.empty();

    // El restore puede eliminar SubGraphs por los que estamos navegando.
    // `active()` recorta el stack al ancestro válido más profundo; lo
    // forzamos aquí para que la próxima llamada a `contextFor` resuelva
    // a un path-key consistente con el estado del grafo restaurado.
    (void)active();
    return true;
}

// ---------------------------------------------------------------------------
// handleZoom
// ---------------------------------------------------------------------------
void NodeCanvas::handleZoom() {
    // Nota: imnodes 0.5 no soporta zoom real del layout (no expone una
    // transformación global de coordenadas).  Lo que sí ofrece es pan vía
    // arrastre del medio-click sobre el canvas vacío + recentrado vía
    // ResetPanning.  En vez de mantener un \"zoom\" que sólo cambia el
    // tamaño del texto y no se nota visiblemente, mantenemos m_zoom = 1.0
    // y exponemos un atajo Home para encuadrar todos los nodos al centro.
    m_zoom = 1.0f;

    const bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    // Frame-all: Home o F (convención de Blender) encuadra todos los
    // nodos en la vista actual.  El renderer calcula bbox + ajusta
    // pan/zoom para que entren con un margen de aire.
    const bool frameAllPressed =
        ImGui::IsKeyPressed(ImGuiKey_Home, false) ||
        (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::IsAnyItemActive());
    if (hov && frameAllPressed && active().nodeCount() > 0) {
        using Space = scinodes::ui::INodeRenderer::CoordSpace;
        constexpr float kInf = 1e9f;
        float minX = kInf, minY = kInf, maxX = -kInf, maxY = -kInf;
        for (const NodeInstance& n : active().nodes()) {
            const auto pos = m_renderer->getNodePosition(n.id, Space::Editor);
            const auto dims = scinodes::ui::computeNodeDimensions(n, &active());
            if (pos.x < minX) minX = pos.x;
            if (pos.y < minY) minY = pos.y;
            if (pos.x + dims.w > maxX) maxX = pos.x + dims.w;
            if (pos.y + dims.h > maxY) maxY = pos.y + dims.h;
        }
        if (minX <= maxX)
            m_renderer->frameToBox({minX, minY}, {maxX, maxY}, 0.f, 0.f);
    }
}



// ---------------------------------------------------------------------------
// addNode — records undo then adds
// ---------------------------------------------------------------------------
int NodeCanvas::addNode(NodeType type) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addNode(type);
    // Spawn en la posición del cursor cuando se abrió el popup.
    // m_popupPos está en screen-space; imnodes opera en grid-space.
    // Aplicamos la transformación inversa para que el nodo aparezca
    // exactamente donde el usuario invocó Shift+A.  Si m_popupPos es
    // (0,0) (no hubo popup, p.ej. add programático), dejamos la
    // posición por defecto de imnodes.
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        m_renderer->setNodePosition(newId, { m_popupPos.x, m_popupPos.y });
    }
    return newId;
}

int NodeCanvas::addCustomNode(const std::string& customType) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addCustomNode(customType);
    if (m_popupPos.x != 0.f || m_popupPos.y != 0.f) {
        m_renderer->setNodePosition(newId, { m_popupPos.x, m_popupPos.y });
    }
    return newId;
}

// ---------------------------------------------------------------------------
// addNodeAt — used by auto-connect / insert-in-edge flows that need to know
// the new node's id so they can wire it up immediately. Equivalent to
// addNode(type) but takes the desired screen-space position explicitly.
// ---------------------------------------------------------------------------
int NodeCanvas::addNodeAt(NodeType type, ImVec2 canvasPos) {
    recordSnapshot(m_graph.snapshot());
    bumpDirty();
    const int newId = active().addNode(type);
    m_renderer->setNodePosition(newId, { canvasPos.x, canvasPos.y });
    return newId;
}

void NodeCanvas::setGraphTitle(const std::string& s) {
    m_graph.setTitle(s);
    bumpDirty();
}
void NodeCanvas::setGraphDescription(const std::string& s) {
    m_graph.setDescription(s);
    bumpDirty();
}
void NodeCanvas::setGraphTags(std::vector<std::string> v) {
    m_graph.setTags(std::move(v));
    bumpDirty();
}

void NodeCanvas::addImportedObject(ImportedObject o) {
    m_graph.addImportedObject(std::move(o));
    bumpDirty();
}
void NodeCanvas::removeImportedObject(const std::string& name) {
    m_graph.removeImportedObject(name);
    bumpDirty();
}

void NodeCanvas::clear() {
    m_history.clear();
    m_graph = NodeGraph{};
    m_positions.clear();
    m_readOnly = false;
    m_applyPositionsPending = false;
    // Bumpear dirty para que cualquier observador (SimController para
    // hot-reload, FileActions para "unsaved changes") sepa que la
    // estructura cambió.  El caller (FileActions::requestNew) llama
    // a markSaved() inmediatamente después si esto fue un "New" limpio.
    bumpDirty();
}

void NodeCanvas::resetView() {
    m_renderer->resetCanvasView();
}

// ---------------------------------------------------------------------------
// Persistence — sync positions, then hand the graph to ScnSerializer
// ---------------------------------------------------------------------------
void NodeCanvas::syncPositionsFromImnodes() {
    // Persistence only handles the top-level graph for now (Fase E
    // pending).  Cambiamos temporalmente al contexto top-level vía
    // pushCanvas/popCanvas; el renderer abstrae el manejo de contextos.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    m_renderer->pushCanvas("/");
    m_positions.clear();
    for (const auto& n : m_graph.nodes()) {
        auto p = m_renderer->getNodePosition(n.id, Space::Editor);
        m_positions[n.id] = ScnVec2{ p.x, p.y };
    }
    m_renderer->popCanvas();
}

void NodeCanvas::applyPositionsToImnodes() {
    // m_positions contiene IDs del top-level.  Si el usuario está
    // navegando dentro de un SubGraph, aplicar posiciones con esos IDs
    // sobre el contexto del subgrafo corrompería estado.  Cambiamos al
    // contexto top-level (push/pop) para que la escritura siempre vaya
    // al editor correcto.
    using Space = scinodes::ui::INodeRenderer::CoordSpace;
    m_renderer->pushCanvas("/");
    for (const auto& [id, p] : m_positions)
        m_renderer->setNodePosition(id, { p.x, p.y }, Space::Editor);
    m_renderer->popCanvas();
}

bool NodeCanvas::saveToFile(const std::string& path) {
    syncPositionsFromImnodes();
    return ScnSerializer::saveToFile(path, m_graph, m_positions);
}

LoadReport NodeCanvas::loadFromFile(const std::string& path) {
    m_history.clear();
    m_positions.clear();
    auto report = ScnSerializer::loadFromFile(path, m_graph, m_positions);
    m_applyPositionsPending = !m_positions.empty();
    m_readOnly = report.ok && report.hasViolations();
    bumpDirty();
    // Auto-load de assets glTF: para cada Device node con `assetPath`
    // no vacío, validar el asset contra el contrato del tipo y cachearlo
    // en m_assetService.  Sin esto el View3DPanel cae al renderer
    // procedural aunque el .scn especifique un .gltf.
    if (m_assetService) {
        // Propaga el directorio del .scn al AssetService para que pueda
        // resolver rutas relativas (assetPath = "examples/dc_motor/...")
        // contra él y sus ancestros — necesario cuando el binario corre
        // desde build/.
        m_assetService->setBaseDir(
            std::filesystem::path(path).parent_path().string());
        for (const auto& n : m_graph.nodes()) {
            if (n.assetPath.empty()) continue;
            if (defOf(n).category != NodeCategory::Device) continue;
            reloadAssetFor(n.id);
        }

        // Auto-load del catálogo by-name (esquema 0.5): hidrata el cache
        // de AssetService con cada entrada de `importedObjects()` parseando
        // su .gltf vía loadCatalog (contract-less).  Sin esto, los nodos
        // Object3D que referencian al catálogo dan placeholder al abrir
        // un .scn — el ImportedObject existe pero ningún DeviceAsset se
        // resuelve via `resolveByName()` hasta que el usuario reimporte
        // manualmente.  Cada entrada del catálogo es independiente: si
        // un .gltf falta o no parsea, se ignora silenciosamente (el
        // Outliner mostrará la entrada del catálogo con 0 parts útiles,
        // y el render dará el placeholder de nuevo).
        for (const auto& obj : m_graph.importedObjects()) {
            if (obj.name.empty() || obj.path.empty()) continue;
            const std::string resolved = m_assetService->resolveAssetPath(obj.path);
            std::string err;
            auto asset = scinodes::DeviceAssetLoader::loadCatalog(resolved, &err);
            if (asset.parts.empty()) continue;
            m_assetService->installNamedAsset(obj.name, std::move(asset));
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// Import — merge un .scn al grafo actual sin destruirlo.
//
// Es esencialmente "paste from file": cargamos en un grafo temporal,
// asignamos IDs frescos a todo lo importado (sin esto chocaríamos
// con IDs existentes), desplazamos las posiciones a la derecha del
// bounding box actual para que la importación quede visible y no
// encima del grafo existente, y re-mapeamos las aristas a los nuevos
// IDs.  Un solo snapshot al inicio hace que Ctrl+Z deshaga la
// importación entera en un golpe.
// ---------------------------------------------------------------------------
NodeCanvas::ImportResult NodeCanvas::importFromFile(const std::string& path) {
    ImportResult out;

    // 1. Cargar a un grafo temporal sin tocar el actual.
    NodeGraph     tempGraph;
    ScnPositions  tempPositions;
    LoadReport rep = ScnSerializer::loadFromFile(path, tempGraph, tempPositions);
    if (!rep.ok) {
        out.ok    = false;
        out.error = rep.fatalError.empty()
                        ? std::string("No pude leer el archivo importado.")
                        : rep.fatalError;
        return out;
    }
    if (tempGraph.nodeCount() == 0) {
        out.ok    = false;
        out.error = "El archivo no contiene nodos para importar.";
        return out;
    }

    // 2. Snapshot único para que el undo deshaga el import entero
    //    — usa recordSnapshot para que las posiciones actuales viajen
    //    con el snapshot (sin esto, Ctrl+Z restauraría el grafo viejo
    //    pero las posiciones quedarían como las del import).
    recordSnapshot(m_graph.snapshot());

    // 3. Bbox del grafo actual a top-level — los nodos importados se
    //    desplazan ABAJO del último nodo (mayor Y).  Importar a la
    //    derecha tiende a salirse de pantalla porque los grafos crecen
    //    horizontalmente; abajo deja al usuario el espacio vertical
    //    libre para reorganizar.  Si no hay nada todavía, offset queda
    //    en (0, 0) y el import cae en el origen.
    constexpr float kImportPaddingY = 120.0f;
    float maxY = -1e30f;
    for (const auto& [id, p] : m_positions)
        if (p.y > maxY) maxY = p.y;
    if (maxY < -1e29f) maxY = 0.0f;
    const float yOffset = m_positions.empty() ? 0.0f
                                              : (maxY + kImportPaddingY);

    // 4. Renumeración: oldId → newId.  Recorremos los nodos del temp
    //    y usamos addNode/addCustomNode que asignan IDs frescos en el
    //    grafo actual.  Para SubGraphs, transplantamos su grafo hijo
    //    al nuevo id vía installSubGraph (sin perder posiciones
    //    internas, que viven en n.position de cada NodeInstance).
    std::unordered_map<int, int> idMap;
    for (const NodeInstance& src : tempGraph.nodes()) {
        int newId = 0;
        if (src.type == NodeType::Custom) {
            newId = m_graph.addCustomNode(src.customType);
        } else if (isSubGraphContainer(src.type)) {
            newId = m_graph.addNode(NodeType::SubGraph);
            if (const NodeGraph* child = tempGraph.subGraphOf(src.id)) {
                NodeGraph copy = *child;
                m_graph.installSubGraph(newId, std::move(copy));
                m_graph.recomputeSubGraphPorts(newId);
            }
        } else {
            newId = m_graph.addNode(src.type);
        }
        idMap[src.id] = newId;

        // Propagar params, stringParams, asset, comment al nodo recién
        // creado en el grafo destino.
        for (const auto& [k, v] : src.params)        m_graph.setParam(newId, k, v);
        for (const auto& [k, v] : src.stringParams)  m_graph.setStringParam(newId, k, v);
        if (!src.assetPath.empty()) m_graph.setAssetPath(newId, src.assetPath);
        if (!src.comment.empty())   m_graph.setComment(newId, src.comment);

        // Posición desplazada hacia abajo — mantenemos también la copia
        // en m_positions para que applyPositionsToImnodes la dibuje en
        // el siguiente frame.
        const ScnVec2 displaced{ src.position.x,
                                 src.position.y + yOffset };
        m_positions[newId] = displaced;
    }

    // 5. Re-mapear aristas: cada attrId codifica un nodeId; lo
    //    reemplazamos por el nuevo.  Si tryAddEdge rechaza alguna,
    //    la registramos en el report — el caller decide si avisa.
    LoadReport reportOut;
    for (const Edge& e : tempGraph.edges()) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        const int newFromAttr = attrRemap(e.fromAttrId, fIt->second);
        const int newToAttr   = attrRemap(e.toAttrId,   tIt->second);
        auto err = m_graph.tryAddEdge(newFromAttr, newToAttr);
        if (err) {
            reportOut.rejectedEdges.push_back({
                fIt->second, tIt->second, err->rule, err->message
            });
        }
    }

    m_applyPositionsPending = true;
    bumpDirty();

    // 6. Marcar los nodos recién importados como seleccionados — el
    //    usuario los puede mover en bloque inmediatamente sin tener
    //    que cazarlos uno por uno.  Sólo a top-level: si la
    //    importación trae SubGraphs, los nodos internos no entran a
    //    la selección (no son visibles al volver al canvas raíz).
    if (m_renderer) {
        m_renderer->pushCanvas("/");
        m_renderer->clearNodeSelection();
        for (const auto& [oldId, newId] : idMap)
            m_renderer->selectNode(newId);
        m_renderer->popCanvas();
    }

    // 7. Cargar assets glTF para los devices importados (mismo flujo
    //    que loadFromFile, pero sólo para los nodos nuevos).
    if (m_assetService) {
        for (const auto& [oldId, newId] : idMap) {
            const NodeInstance* n = m_graph.findNode(newId);
            if (!n || n->assetPath.empty()) continue;
            m_assetService->reload(newId, typeName(n->type), n->assetPath);
        }
    }

    out.ok          = true;
    out.report      = reportOut;
    out.report.ok   = true;
    out.report.nodesLoaded = static_cast<int>(idMap.size());
    out.report.edgesLoaded = tempGraph.edgeCount()
                             - static_cast<int>(reportOut.rejectedEdges.size());
    return out;
}

void NodeCanvas::reloadAssetFor(int nodeId) {
    if (!m_assetService) return;
    const NodeInstance* n = active().findNode(nodeId);
    if (!n) { m_assetService->detach(nodeId); return; }
    m_assetService->reload(nodeId, typeName(n->type), n->assetPath);
}

void NodeCanvas::detachAsset(int nodeId) {
    active().setAssetPath(nodeId, "");
    if (m_assetService) m_assetService->detach(nodeId);
}

const std::unordered_map<int, scinodes::DeviceAsset>&
NodeCanvas::loadedAssets() const {
    static const std::unordered_map<int, scinodes::DeviceAsset> kEmpty;
    return m_assetService ? m_assetService->all() : kEmpty;
}

void NodeCanvas::openMappingPanelFor(int nodeId) {
    const NodeInstance* n = active().findNode(nodeId);
    if (!n || n->assetPath.empty()) return;

    const auto* contract =
        (m_contractRegistry ? m_contractRegistry->find(typeName(n->type)) : nullptr);
    if (!contract) return;

    const std::string resolved =
        m_assetService ? m_assetService->resolveAssetPath(n->assetPath)
                       : n->assetPath;
    if (m_mappingPanel.openFor(resolved, *contract)) {
        m_mappingNodeId = nodeId;
    } else {
        m_errorMsg   = "No se pudo abrir el panel de mapping (¿glTF inválido?)";
        m_errorTimer = 5.0f;
    }
}
