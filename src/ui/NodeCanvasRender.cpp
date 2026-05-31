#include "NodeCanvas.hpp"
#include "NodeCanvasInternal.hpp"
#include "canvas/Canvas.hpp"
#include "../app/AssetService.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/CustomNodeRegistry.hpp"
#include "../core/I18n.hpp"
#include "../core/Quantity.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// NodeCanvasRender — split de NodeCanvas.cpp (etapa 6K.C).  Contiene los
// métodos que dibujan cada nodo, las aristas, el breadcrumb de SubGraph y
// el tooltip de error.  Sin lógica de input / dispatch / persistence — esos
// viven en sus propios .cpp.
//
// Helpers de color (titleCol / titleHovCol / wireCol) viven en
// NodeCanvasInternal.hpp como `inline` para compartirlos sin tocar el
// linkage.
// ---------------------------------------------------------------------------
using scinodes::ui::canvas_detail::titleCol;
using scinodes::ui::canvas_detail::titleHovCol;
using scinodes::ui::canvas_detail::wireCol;

// ---------------------------------------------------------------------------
// drawNode
// ---------------------------------------------------------------------------
void NodeCanvas::drawNode(const NodeInstance& n) {
    const NodeDef& def = defOf(n);

    const bool  highlighted   = (m_highlightNodeId == n.id);
    // Alias (etapa 6I.U): color distinto al verde Source para que se
    // vea de un vistazo que es un nodo "virtual" — amarillo/oro. Las
    // Sources reales (StepSignal, VoltageSource) se mantienen verdes.
    auto categoryColor = [&]() -> ImU32 {
        if (isAliasType(n.type))
            return IM_COL32(170, 130,  40, 255);   // ámbar
        return titleCol(def.category);
    };
    auto categoryHovColor = [&]() -> ImU32 {
        if (isAliasType(n.type))
            return IM_COL32(210, 170,  70, 255);
        return titleHovCol(def.category);
    };
    const ImU32 titleColor    = highlighted ? IM_COL32(220, 50, 50, 255)
                                            : categoryColor();
    const ImU32 titleHovColor = highlighted ? IM_COL32(240, 80, 80, 255)
                                            : categoryHovColor();
    using CK = scinodes::ui::INodeRenderer::ColorKey;
    m_renderer->pushColor(CK::TitleBar,         titleColor);
    m_renderer->pushColor(CK::TitleBarHovered,  titleHovColor);
    m_renderer->pushColor(CK::TitleBarSelected, titleHovColor);

    m_renderer->beginNode(n.id, scinodes::ui::computeNodeDimensions(n, &active()),
                          /*hasComment*/ !n.comment.empty());

    m_renderer->beginNodeTitleBar();
    // SubGraph: si tiene `Name` en stringParams, usarlo como título.
    // Para el resto: clave i18n `node.<typeName>.label` con fallback al
    // def.label del registry (mantiene el nombre original si no hay
    // traducción).  Custom nodes y stubs caen al fallback también.
    {
        // El nombre custom (stringParams["Name"]) tiene prioridad sobre
        // el label del registry para TODOS los nodos (etapa 6I.T).
        // Para Alias (etapa 6I.U), el título por default refleja el
        // TARGET al que apunta: "→ <Name del target>" o el id si no
        // hay target asignado.  El user puede sobrescribir con F2.
        std::string title;
        auto it = n.stringParams.find("Name");
        if (it != n.stringParams.end() && !it->second.empty()) {
            title = it->second;
        }
        if (title.empty() && isAliasType(n.type)) {
            auto pt = n.params.find("target_node_id");
            const int tid = (pt != n.params.end())
                ? static_cast<int>(pt->second) : 0;
            const NodeInstance* tgt = active().findNode(tid);
            if (tgt) {
                auto tit = tgt->stringParams.find("Name");
                if (tit != tgt->stringParams.end() && !tit->second.empty()) {
                    title = "→ " + tit->second;
                } else {
                    // Localizar el label del target via i18n.  Sin
                    // esto, un Alias a Gain mostraría "→ Gain" aunque
                    // el resto del catálogo esté en español.
                    const std::string key = std::string("node.")
                        + typeName(tgt->type) + ".label";
                    title = "→ " + scinodes::trOr(key, labelOf(tgt->type));
                }
            } else {
                title = scinodes::tr("alias.unassigned");
            }
        }
        if (title.empty()) {
            const std::string key =
                std::string("node.") + typeName(n.type) + ".label";
            title = scinodes::trOr(key, def.label);
        }
        ImGui::TextUnformatted(title.c_str());
    }
    m_renderer->endNodeTitleBar();

    // Para sinks multi-canal dinámicos (Oscilloscope, SceneOutput)
    // renderizamos sólo los puertos en uso + 1 extra "vacío" para
    // conectar la siguiente señal/geometría.  Si todos los puertos
    // están ocupados, no mostramos extra.  Resto de nodos: número
    // fijo de puertos.
    int portsToShow = def.inputPorts;
    if (n.type == NodeType::Oscilloscope ||
        n.type == NodeType::SceneOutput) {
        int used = 0;
        for (const auto& e : active().edges())
            if (e.toNodeId == n.id) ++used;
        portsToShow = std::min(def.inputPorts, used + 1);
    }
    // Helper: true si algún edge del grafo activo toca este attrId.
    // Sirve para distinguir visualmente pines conectados (color
    // brillante) de pines libres (gris tenue) — feedback que faltaba
    // y se identificó como gap UX durante el run de E1.
    auto isAttrConnected = [&](int attrId) -> bool {
        for (const auto& e : active().edges())
            if (e.fromAttrId == attrId || e.toAttrId == attrId)
                return true;
        return false;
    };
    // Color del pin: la GRAMÁTICA decide.  `pinColorFromType` deriva
    // el color de la forma del TypeExpr (scalar=azul, vec=violeta,
    // mat=naranja, geometry=cyan, ...).  Si el pin está desconectado,
    // atenuamos el alpha; si está conectado, full brillo.
    // Antes (etapa 1): tabla enum→color hardcoded.  Ahora: una sola
    // llamada que future-proof cualquier TypeExpr nuevo.
    constexpr unsigned int kPinDisconnectedAlpha = 200;
    constexpr unsigned int kPinConnectedAlpha    = 255;
    auto attenuate = [](unsigned int color, unsigned int alpha) -> unsigned int {
        return (color & 0x00FFFFFFu) | ((alpha & 0xFFu) << 24);
    };
    using CKp = scinodes::ui::INodeRenderer::ColorKey;

    auto pinColorIn = [&](int port) -> unsigned int {
        const bool conn = isAttrConnected(n.inputAttrId(port));
        const unsigned int base = pinColorFromType(inputPortTypeOf(def, port));
        return attenuate(base, conn ? kPinConnectedAlpha : kPinDisconnectedAlpha);
    };
    auto pinColorOut = [&](int port) -> unsigned int {
        const bool conn = isAttrConnected(n.outputAttrId(port));
        const unsigned int base = pinColorFromType(outputPortTypeOf(def, port));
        return attenuate(base, conn ? kPinConnectedAlpha : kPinDisconnectedAlpha);
    };


    for (int p = 0; p < portsToShow; ++p) {
        const int aid = n.inputAttrId(p);
        m_renderer->pushColor(CKp::Pin, pinColorIn(p));
        m_renderer->beginInputAttribute(aid,
                                        scinodes::ui::PortShape::CircleFilled);
        // Label del puerto.  Dos fuentes con semánticas distintas:
        //   - stringParams["portLabel<p>"]  → ANOTACIÓN per-instance
        //     (Oscilloscope: "in N  θ(t)" preserva el índice + la
        //     etiqueta editable del canal).
        //   - def.inputPortLabels[p]        → label CANÓNICO per-type
        //     desde el registry (TransformObject: "geometría" sin
        //     prefijo "in 1" — el nombre identifica al puerto).
        std::string instanceLabel;
        {
            char k[32]; std::snprintf(k, sizeof(k), "portLabel%d", p);
            auto it = n.stringParams.find(k);
            if (it != n.stringParams.end()) instanceLabel = it->second;
        }
        // Etapa 6L: label canónico via i18n (clave
        // `node.<TypeName>.input_label.<port>`).  Fallback al texto del
        // registry — bundles que no traduzcan estos keys siguen viendo
        // el inglés del NodeDef.
        const std::string canonical =
            (p < static_cast<int>(def.inputPortLabels.size()))
                ? def.inputPortLabels[p] : std::string{};
        const std::string typeLabel = scinodes::trOr(
            std::string("node.") + typeName(n.type) +
            ".input_label." + std::to_string(p),
            canonical);

        // Live-value: para puertos signal de TransformObject (paso 5c
        // del refactor 3D + UX), si hay bridge y hay un Sink tap
        // downstream del source, mostramos el valor actual al lado del
        // label.  Da feedback de "qué está transformando ahora".  El
        // walker comparte la lógica con SceneCollector::readLiveSampleAt.
        std::string liveSuffix;
        if (m_bridge && n.type == NodeType::TransformObject &&
            p >= 1 && p <= 3 && isAttrConnected(aid)) {
            const Edge* inEdge = nullptr;
            for (const auto& e : active().edges()) {
                if (e.toNodeId == n.id &&
                    attrInputPort(e.toAttrId) == p) { inEdge = &e; break; }
            }
            if (inEdge) {
                // Buscar un Sink "tap" en el mismo cable del source.
                const int srcId   = inEdge->fromNodeId;
                const int srcPort = attrOutputPort(inEdge->fromAttrId);
                for (const auto& e : active().edges()) {
                    if (e.fromNodeId != srcId)               continue;
                    if (attrOutputPort(e.fromAttrId) != srcPort) continue;
                    const NodeInstance* dst = active().findNode(e.toNodeId);
                    if (!dst) continue;
                    if (defOf(*dst).category != NodeCategory::Sink) continue;
                    const int channel = attrIsInput(e.toAttrId)
                                          ? attrInputPort(e.toAttrId) : 0;
                    auto buf = m_bridge->buffer(dst->id, channel);
                    if (buf.empty()) continue;
                    char tmp[32];
                    std::snprintf(tmp, sizeof(tmp), " = %.3g", buf.back());
                    liveSuffix = tmp;
                    break;
                }
            }
        }

        // Sufijo de unidad declarada — read-only, parte del label para
        // no añadir widgets que se salgan del nodo.  Sin sufijo si el
        // puerto es polimórfico (la unidad se decide por contexto, no
        // tiene sentido mostrarla acá; el override va por otro flujo).
        std::string unitSuffix;
        if (hasDeclaredInputUnit(def, p)) {
            std::string u = inputPortUnitOf(def, p).toCanonicalString();
            if (u.empty()) u = "1";   // adimensional canonicalizada
            unitSuffix = "  [" + u + "]";
        }

        if (def.inputPorts == 1 && instanceLabel.empty() && typeLabel.empty())
            ImGui::Text("in%s%s", liveSuffix.c_str(), unitSuffix.c_str());
        else if (!typeLabel.empty())
            // Canonical type-level label — reemplaza "in N" porque el
            // nombre del puerto ES su identidad ("geometría" no necesita
            // que le diga "in 1").
            ImGui::Text("%s%s%s", typeLabel.c_str(), liveSuffix.c_str(),
                                  unitSuffix.c_str());
        else if (!instanceLabel.empty())
            // Anotación per-instance — preserva el índice ordinal.
            ImGui::Text("in %d  %s%s%s", p + 1, instanceLabel.c_str(),
                                          liveSuffix.c_str(),
                                          unitSuffix.c_str());
        else
            ImGui::Text("in %d%s%s", p + 1, liveSuffix.c_str(),
                                      unitSuffix.c_str());
        m_renderer->endInputAttribute();
        m_renderer->popColor();
    }

    // Columna de valores alineada: el DragFloat de cada parámetro empieza
    // a la misma X dentro del nodo, sin importar el largo del label.
    // Medimos primero el label más ancho con la fuente actual (que ya
    // refleja el zoom via SetWindowFontScale).  Luego cada fila usa
    // SetCursorScreenPos para llevar el cursor a esa columna.
    // El label puede ser una traducción i18n (clave
    // `node.<type>.param.<paramId>`) o el `pd.name` literal como
    // fallback — el identificador del param sigue siendo `pd.name`,
    // que es key del mapa n.params y nombre Scilab emitido.
    auto paramDisplay = [&](const ParamDef& pd) -> std::string {
        const std::string key = std::string("node.")
                              + typeName(n.type) + ".param." + pd.name;
        return scinodes::trOr(key, pd.name);
    };

    float labelColMaxW = 0.f;
    for (const auto& pd : def.params) {
        const float w = ImGui::CalcTextSize(paramDisplay(pd).c_str()).x;
        if (w > labelColMaxW) labelColMaxW = w;
    }
    const float valueColGap = ImGui::GetStyle().ItemSpacing.x;

    for (int i = 0; i < (int)def.params.size(); ++i) {
        const auto& pd  = def.params[i];

        // Alias (etapa 6I.U): los params `target_node_id` y `target_port`
        // son IDENTIFICADORES del nodo referenciado, no señales
        // numéricas.  No tiene sentido un pin para enrutar otra señal
        // hacia ellos (no son magnitudes físicas).  El selector vive
        // en el param panel (dropdown).  Acá los escondemos del cuerpo.
        if (isAliasType(n.type)) continue;

        // Per-param pin (PR2 v1.1): cada row tiene un pin a la izquierda
        // que acepta un edge de una señal upstream.  Si está conectado,
        // el valor del widget queda decorativo — el solver consume la
        // expresión del source en su lugar (ver ScilabCodeGen).
        const int  paramAttr   = n.paramAttrId(i);
        const bool paramDriven = isAttrConnected(paramAttr);
        const unsigned int paramBase = pinColorFromType(exprScalar());
        m_renderer->pushColor(CKp::Pin,
            attenuate(paramBase,
                      paramDriven ? kPinConnectedAlpha : kPinDisconnectedAlpha));
        m_renderer->beginParamAttribute(paramAttr,
                                        scinodes::ui::PortShape::CircleFilled);

        // Label (traducido si hay clave i18n; fallback al pd.name).
        ImGui::TextDisabled("%s", paramDisplay(pd).c_str());
        const float labelStartX = ImGui::GetItemRectMin().x;
        ImGui::SameLine();
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({ labelStartX + labelColMaxW + valueColGap,
                                    cp.y });

        // ---- QuantityField (etapa 6I.F) ------------------------------
        // Único InputText que muestra "<value> <unit>" combinado.
        // Reemplaza el DragFloat + TextDisabled(unit) anterior: el
        // parser de unidades opera sobre el texto entero, así que
        // tipear "100cm" o "3.3kV" funciona end-to-end.
        //
        // Display: canonicalizamos la Quantity al displayUnits del
        // grafo antes de formatearla — si el proyecto declara
        // length=m, un field con {100, cm} se muestra como "1 m".
        // Sin perder el storage interno (que sigue siendo {100, cm}
        // hasta que el usuario edite).
        //
        // Commit: parseQuantity al string editado.  Si !hasUnit
        // (usuario tipeó sólo un número), preservamos la unidad
        // anterior — comportamiento estilo Blender que evita borrar
        // accidentalmente la unidad al ajustar un valor.
        scinodes::Quantity curQ;
        auto fIt = n.fields.find(pd.name);
        if (fIt != n.fields.end()) curQ = fIt->second;
        else                       curQ.value = n.params.at(pd.name);
        const scinodes::Quantity displayQ =
            active().canonicalizeForDisplay(curQ);
        const std::string display = scinodes::toDisplayString(displayQ);

        char buf[64];
        std::strncpy(buf, display.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char wid[32];
        std::snprintf(wid, sizeof(wid), "##qf%d_%d", n.id, i);
        const ImGuiID wgtId = ImGui::GetID(wid);
        if (paramDriven) ImGui::BeginDisabled();

        if (ImGui::IsItemActivated())
            m_pendingParamBefore = m_graph.snapshot();

        // Ancho específico del QuantityField — más amplio que el
        // default del renderer (kNodeWidgetWidth=86, pensado para
        // DragFloat de sólo número).  Escalamos por la ratio de
        // model-units para respetar el zoom (que ya está aplicado en
        // CalcItemWidth via el renderer).
        const float qfRatio = scinodes::ui::kNodeQuantityFieldWidth
                            / scinodes::ui::kNodeWidgetWidth;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * qfRatio);
        const bool committed = ImGui::InputTextWithHint(
            wid, "value unit",
            buf, sizeof(buf),
            ImGuiInputTextFlags_EnterReturnsTrue);

        if (committed) {
            // Etapa 6I.M: parseQuantity context-aware — "2k" en un
            // Ohm field se interpreta como "2 kΩ", "2k" en V-field como
            // "2 kV", etc.  El contexto es la unidad ACTUAL del field
            // (no la declarada del registry — para que el usuario pueda
            // empezar con un override y seguir usando prefijos).
            auto pr = scinodes::parseQuantity(buf, curQ.unit);
            // Aceptación en tres casos:
            //   1. parse OK + sólo número  → preserva unidad anterior
            //   2. parse OK + curQ adimensional → polimórfico, acepta
            //      cualquier dimensión (Kp puede pasar de "1" a "5V/V")
            //   3. parse OK + sameDimension(curQ.unit, new) → cambio
            //      coherente (m↔cm, V↔kV, Ohm↔kΩ)
            // En cualquier otro caso, el commit se descarta — el frame
            // siguiente reconstruye `buf` desde el estado persistido y
            // el edit visualmente desaparece, indicando rechazo.  Este
            // es R7 a nivel de field: la dimensión declarada por el
            // registry NO se corrompe escribiendo metros en una
            // resistencia.
            if (pr.ok()) {
                scinodes::Quantity q = pr.quantity;
                bool accept = false;
                if (!pr.hasUnit) {
                    // Etapa 6I.N: bare number → reset a la unidad
                    // CANÓNICA SI de la dimensión del field, NO preserva
                    // el prefijo previo.  El grafo trabaja en SI; "1"
                    // en un Ohm-field es "1 Ohm" aunque el último input
                    // hubiera sido "5 mΩ".  Mantiene la dimensión
                    // (exp signature) pero resetea magnitude=1.0.
                    q.unit = curQ.unit;
                    q.unit.magnitude = 1.0;
                    accept = true;
                } else if (curQ.unit.isDimensionless()) {
                    // Etapa 6I.P: el field es IDEAL (Kp, signos,
                    // coeficientes).  Sólo acepta inputs adimensionales.
                    // Tipear "5 mV" en Kp → rechaza (Kp no transporta
                    // V; lo que se construye con Kp es una expresión
                    // que adquiere unidad por el contexto, no Kp).
                    // Acepta prefijos (k, M, m, μ) que aplican sólo
                    // al escalar.
                    if (q.unit.isDimensionless()) accept = true;
                } else if (q.unit.sameDimension(curQ.unit)) {
                    accept = true;
                }
                if (accept) {
                    active().setFieldQuantity(n.id, pd.name, q);
                    if (m_paramCallback)
                        m_paramCallback(pathFor(n.id), i, q.toSI());
                }
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && m_pendingParamBefore) {
            recordSnapshot(*m_pendingParamBefore);
            m_pendingParamBefore = std::nullopt;
        }

        if (paramDriven) ImGui::EndDisabled();
        (void)wgtId;  // reservado para futura introspección de TempInputIsActive

        m_renderer->endParamAttribute();
        m_renderer->popColor();
    }

    for (int p = 0; p < def.outputPorts; ++p) {
        const int aid = n.outputAttrId(p);
        // Construimos el texto ANTES de beginOutputAttribute para
        // poder pasar el ancho real (etapa 6I.F.2 — antes el renderer
        // hardcoded 5 chars y "out [rad/s]" se salía del nodo).
        // Etapa 6L: label canónico via i18n con fallback al registry.
        const std::string outCanonical =
            (p < static_cast<int>(def.outputPortLabels.size()))
                ? def.outputPortLabels[p] : std::string{};
        const std::string outLabel = scinodes::trOr(
            std::string("node.") + typeName(n.type) +
            ".output_label." + std::to_string(p),
            outCanonical);
        std::string unitSuffix;
        if (hasDeclaredOutputUnit(def, p)) {
            std::string u = outputPortUnitOf(def, p).toCanonicalString();
            if (u.empty()) u = "1";
            unitSuffix = "  [" + u + "]";
        }
        std::string fullLabel;
        if (!outLabel.empty())                fullLabel = outLabel;
        else if (def.outputPorts == 1)        fullLabel = "out";
        else                                  fullLabel = "out " + std::to_string(p + 1);
        fullLabel += unitSuffix;

        m_renderer->pushColor(CKp::Pin, pinColorOut(p));
        m_renderer->beginOutputAttribute(aid,
                                         scinodes::ui::PortShape::CircleFilled,
                                         static_cast<int>(fullLabel.size()));
        ImGui::TextUnformatted(fullLabel.c_str());
        m_renderer->endOutputAttribute();
        m_renderer->popColor();
    }

    m_renderer->endNode();
    m_renderer->popColor(3);
}
// ---------------------------------------------------------------------------
// drawEdges — renders all edges with category-coded wire colours
// ---------------------------------------------------------------------------
void NodeCanvas::drawEdges() {
    using CK = scinodes::ui::INodeRenderer::ColorKey;
    for (const auto& e : active().edges()) {
        ImU32 wc = wireCol(e.fromNodeId, m_graph);
        m_renderer->pushColor(CK::Link,         wc);
        m_renderer->pushColor(CK::LinkHovered,  wc);
        m_renderer->pushColor(CK::LinkSelected, IM_COL32(255, 220, 50, 255));
        m_renderer->drawLink(e.id, e.fromAttrId, e.toAttrId);
        m_renderer->popColor(3);
    }
}
void NodeCanvas::drawBreadcrumb() {
    ImGui::TextDisabled(" path:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Top")) exitToLevel(0);
    NodeGraph* g = &m_graph;
    for (size_t i = 0; i < m_canvasStack.size(); ++i) {
        ImGui::SameLine(); ImGui::TextUnformatted(" / "); ImGui::SameLine();
        int id = m_canvasStack[i];
        // Usar el `Name` (stringParam) si existe, sino "SubGraph[id]".
        std::string label = "SubGraph";
        if (auto* n = g->findNode(id)) {
            auto it = n->stringParams.find("Name");
            if (it != n->stringParams.end() && !it->second.empty())
                label = it->second;
        }
        char btn[96];
        std::snprintf(btn, sizeof(btn), "%s##bc%zu", label.c_str(), i);
        if (ImGui::SmallButton(btn)) {
            exitToLevel(static_cast<int>(i) + 1);
            return;
        }
        if (auto* child = g->subGraphOf(id)) g = child;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("   |  F2 to rename selected SubGraph");
    ImGui::Separator();
}
// ---------------------------------------------------------------------------
// showErrorTooltip — fades out after m_errorTimer seconds
// ---------------------------------------------------------------------------
void NodeCanvas::showErrorTooltip() {
    if (m_errorTimer <= 0.f) return;
    m_errorTimer -= ImGui::GetIO().DeltaTime;

    ImGui::SetNextWindowPos(
        { ImGui::GetWindowPos().x + 12.f,
          ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 60.f });
    ImGui::SetNextWindowBgAlpha(std::min(1.f, m_errorTimer));
    ImGui::BeginTooltip();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 90, 90, 255));
    ImGui::TextUnformatted(m_errorMsg.c_str());
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}
