#include "Canvas.hpp"

#include "../../core/I18n.hpp"
#include "../../core/NodeInstance.hpp"
#include "../../core/NodeType.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>

namespace scinodes::ui {

Canvas::Canvas(NodeGraph& graph) : m_graph(&graph) {}

CanvasPos Canvas::positionOf(int nodeId) const {
    auto it = m_positions.find(nodeId);
    if (it != m_positions.end()) return it->second;
    return {};
}

void Canvas::setPositionOf(int nodeId, CanvasPos pos) {
    m_positions[nodeId] = pos;
}

// ---------------------------------------------------------------------------
// computeNodeDimensions — función libre.  Única fuente de verdad para el
// tamaño de un nodo a partir del modelo.  Equivalente conceptual del
// `node_update_basis()` de Blender (la pasada de layout previa al draw),
// adaptada a la simpleza estructural de SciNodes: como nuestros widgets
// internos son siempre los mismos (texto + DragFloat + texto-unidad +
// labels de puertos), podemos aproximar los anchos por el número de
// caracteres × `kNodeCharWidth` en vez de medir con `ImGui::CalcTextSize`.
//
// La regla del layout es: la caja debe ser tan ancha como su contenido
// más largo (título, fila de parámetro, label de puerto custom).  Sin
// esto los DragFloat y los labels custom (p. ej. "Salida y(t) — lazo
// cerrado" en un puerto del Oscilloscope) se salían del recuadro.
// ---------------------------------------------------------------------------
namespace {

float textWidthApprox(const std::string& s) {
    return kNodeCharWidth * static_cast<float>(s.size());
}

}  // namespace

CanvasDims computeNodeDimensions(const NodeInstance& n) {
    // Resolver el def con defOf — funciona uniformemente para builtin,
    // SubGraph y Custom (cada uno expone params, inputPorts, outputPorts
    // a través de su NodeDef sintetizado).  Los SubGraphInput/Output
    // antes tenían un atajo de 130x70 px, pero al traducir el título
    // a "Entrada de SubGrafo" (~152 px) el texto se salía de la caja.
    // Caen ahora al path general que mide el label real.
    const NodeDef& def = defOf(n);

    // ---- Ancho del título ----------------------------------------------
    // El título visible puede ser una traducción i18n (clave
    // `node.<type>.label`) — el cálculo de ancho debe usar exactamente
    // el mismo string que el renderer dibujará, o si no la caja queda
    // chica al cambiar de idioma a uno con palabras más largas (bug
    // visible al traducir "Transfer Function (2nd)" a "Función de
    // transferencia (2do orden)").
    // Etapa 6I.T: el Name custom aplica a TODOS los nodos.  Computa
    // el width con el nombre real visible (sea custom o label del
    // registry) para que el nodo no se vea apretado.
    std::string title;
    {
        auto it = n.stringParams.find("Name");
        if (it != n.stringParams.end() && !it->second.empty())
            title = it->second;
    }
    if (title.empty()) {
        const std::string key = std::string("node.")
                              + typeName(n.type) + ".label";
        title = scinodes::trOr(key, def.label);
    }
    float maxContentW = textWidthApprox(title);

    // ---- Ancho de cada fila de parámetro (pin + label + DragFloat + unit) ----
    // Igual que con el título: usamos el label traducido del param para
    // que la columna del label refleje su tamaño real en pantalla.  Cada
    // row ahora reserva espacio para el PIN de input del param (estilo
    // Blender) — al inicio del row, antes del label.
    constexpr float kParamPinReserve = 2.f * kNodePinRadius + kNodeRowGap;
    // Alias (etapa 6I.U.b): drawNode esconde los params del cuerpo
    // (target_node_id/port son identificadores en el panel), así que
    // tampoco deben contar para el width acá — sino el nodo queda
    // ancho como cualquier otro con params largos.
    const bool skipParamsForWidth = (n.type == NodeType::Alias);
    for (const auto& pd : def.params) {
        if (skipParamsForWidth) break;
        const std::string paramLbl = scinodes::trOr(
            std::string("node.") + typeName(n.type) + ".param." + pd.name,
            pd.name);
        // El QuantityField (etapa 6I.F) reemplazó al DragFloat + texto
        // de unidad por un único InputText con "valor unidad" — usa el
        // ancho amplio para que unidades compuestas no se trunquen.
        float rowW = kParamPinReserve
                   + textWidthApprox(paramLbl)
                   + kNodeRowGap + kNodeQuantityFieldWidth;
        if (rowW > maxContentW) maxContentW = rowW;
    }

    // ---- Ancho de los labels de puerto (incluye port-labels custom
    //      como los del Oscilloscope guardados en stringParams + sufijo
    //      de unidad declarada del registry, agregado en etapa 6I.F).
    // Sin contar el sufijo `[unit]` el width quedaba corto y el "rad/s"
    // de la salida del Motor DC se cortaba como "[r...]".
    auto unitSuffixLen = [](const scinodes::Unit& u) -> size_t {
        std::string s = u.toCanonicalString();
        if (s.empty()) s = "1";          // adimensional × 1
        return 2 /*[]*/ + 2 /*gap*/ + s.size();
    };
    for (int p = 0; p < def.inputPorts; ++p) {
        // Tres fuentes de label, en orden de precedencia que usa drawNode:
        //   1. stringParams["portLabel<i>"] — anotación per-instance
        //      (Oscilloscope etc.).  Si existe, se muestra como
        //      "in N  <custom>".
        //   2. def.inputPortLabels[i] — label CANÓNICO per-type del
        //      registry (TransformObject: "rotación  [rad, Euler XYZ]").
        //      Reemplaza "in N" completamente.
        //   3. Fallback genérico "in" / "in N".
        // Antes computeNodeDimensions sólo miraba (1) y (3), no (2) —
        // por eso los labels declarados largos (TransformObject) se
        // salían del nodo.
        std::string lbl;
        char key[32]; std::snprintf(key, sizeof(key), "portLabel%d", p);
        auto it = n.stringParams.find(key);
        if (it != n.stringParams.end() && !it->second.empty()) {
            lbl = "in " + std::to_string(p + 1) + "  " + it->second;
        } else if (p < static_cast<int>(def.inputPortLabels.size()) &&
                   !def.inputPortLabels[p].empty()) {
            lbl = def.inputPortLabels[p];
        } else if (def.inputPorts == 1) {
            lbl = "in";
        } else {
            lbl = "in " + std::to_string(p + 1);
        }

        size_t totalChars = lbl.size();
        if (hasDeclaredInputUnit(def, p))
            totalChars += unitSuffixLen(inputPortUnitOf(def, p));

        const float lblW = kNodeCharWidth * static_cast<float>(totalChars)
                         + 2.f * kNodePinRadius;
        if (lblW > maxContentW) maxContentW = lblW;
    }
    for (int p = 0; p < def.outputPorts; ++p) {
        // Idem para outputs — preferir def.outputPortLabels (SeparateXYZ
        // declara "x"/"y"/"z") sobre el fallback "out N".
        std::string lbl;
        if (p < static_cast<int>(def.outputPortLabels.size()) &&
            !def.outputPortLabels[p].empty()) {
            lbl = def.outputPortLabels[p];
        } else if (def.outputPorts == 1) {
            lbl = "out";
        } else {
            lbl = "out " + std::to_string(p + 1);
        }
        size_t totalChars = lbl.size();
        if (hasDeclaredOutputUnit(def, p))
            totalChars += unitSuffixLen(outputPortUnitOf(def, p));

        const float lblW = kNodeCharWidth * static_cast<float>(totalChars)
                         + 2.f * kNodePinRadius;
        if (lblW > maxContentW) maxContentW = lblW;
    }

    // Alias (etapa 6I.U.b): el nodo se ajusta exactamente al contenido,
    // sin pisos kNodeMinWidth/Height — su rol es indicar visualmente
    // "esto es un alias", y a menor footprint, menos ruido visual.
    // El resto de nodos mantiene los pisos.
    const bool tightFit = (n.type == NodeType::Alias);
    const float w = tightFit
        ? (maxContentW + 2.f * kNodePadInner)
        : std::max(kNodeMinWidth, maxContentW + 2.f * kNodePadInner);

    // Layout vertical: titleH + padInner + (inputs+params+outputs)*rowH + padInner.
    // El renderer avanza el cursor por kNodeRowHeight en cada begin*Attr,
    // así caja y contenido siempre coinciden.
    //
    // Alias: los params target_node_id/target_port no se renderizan
    // como rows en el cuerpo (su selector vive en el param panel),
    // así que no cuentan para la altura.
    const int paramRows = (n.type == NodeType::Alias)
                          ? 0
                          : static_cast<int>(def.params.size());
    const int totalRows = def.inputPorts + def.outputPorts + paramRows;
    const float hExact = kNodeTitleHeight + kNodePadInner * 2.f
                        + totalRows * kNodeRowHeight;
    const float h = tightFit ? hExact
                             : std::max(kNodeMinHeight, hExact);
    return { w, h };
}

CanvasDims Canvas::dimensionsOf(int nodeId) const {
    const NodeInstance* n = m_graph->findNode(nodeId);
    if (!n) return { kNodeMinWidth, kNodeMinHeight };
    return computeNodeDimensions(*n);
}

Canvas* Canvas::subCanvasOf(int subGraphNodeId) {
    NodeGraph* childGraph = m_graph->subGraphOf(subGraphNodeId);
    if (!childGraph) return nullptr;
    auto it = m_subCanvases.find(subGraphNodeId);
    if (it != m_subCanvases.end()) return it->second.get();
    auto inserted =
        m_subCanvases.emplace(subGraphNodeId,
                              std::make_unique<Canvas>(*childGraph));
    return inserted.first->second.get();
}

const Canvas* Canvas::subCanvasOf(int subGraphNodeId) const {
    // Lazy-create también en const path para mantener invariante de
    // existencia simétrica con el subGraph del modelo.  El mutable de
    // m_subCanvases es por esto.
    const NodeGraph* childGraph = m_graph->subGraphOf(subGraphNodeId);
    if (!childGraph) return nullptr;
    auto it = m_subCanvases.find(subGraphNodeId);
    if (it != m_subCanvases.end()) return it->second.get();
    // Cast away const para crear el sub-canvas — equivalente al patrón
    // mutable-cache; el modelo no se muta, solo nuestra tabla derivada.
    auto* self = const_cast<Canvas*>(this);
    NodeGraph* childMutable = self->m_graph->subGraphOf(subGraphNodeId);
    auto inserted = self->m_subCanvases.emplace(
        subGraphNodeId, std::make_unique<Canvas>(*childMutable));
    return inserted.first->second.get();
}

// ---------------------------------------------------------------------------
// autoLayout — topo-sort con ruptura de feedback en nodos pure-state.
//
// Algoritmo Kahn estándar, salvo que los edges que entran a un nodo
// pure-state (Integrator, LowPassFilter, TF, DCMotorModel, etc.) NO cuentan
// para el in-degree del destino.  Esa regla refleja la semántica que
// ScilabCodeGen ya usa: en un lazo cerrado, el integrador "rompe" el
// ciclo porque su salida en el paso t depende del estado, no de la
// entrada del mismo paso.  Aplicada al layout, permite que un lazo PID
// + planta + feedback se aplane en columnas en lugar de colapsar a una.
//
// Lo único que necesitábamos del codegen era esta regla; el resto del
// algoritmo es un BFS por niveles común.
// ---------------------------------------------------------------------------
void Canvas::autoLayout() {
    const auto& nodes = m_graph->nodes();
    if (nodes.empty()) return;

    std::unordered_map<int, std::vector<int>>   incoming;
    std::unordered_map<int, std::vector<int>>   outgoing;
    for (const auto& n : nodes) {
        incoming[n.id] = {};
        outgoing[n.id] = {};
    }
    for (const auto& e : m_graph->edges()) {
        outgoing[e.fromNodeId].push_back(e.toNodeId);
        incoming[e.toNodeId].push_back(e.fromNodeId);
    }

    // Detección de back-edges vía DFS coloreado.  Un back-edge es una
    // arista (u, v) donde v está EN LA PILA del DFS actual — cierra un
    // ciclo.  Ignorando esas aristas, el grafo se vuelve un DAG y el
    // Kahn estándar lo procesa sin trabarse.  Es el enfoque clásico
    // de Sugiyama para layouts de grafos cíclicos: identificar las
    // back-edges, ignorarlas (o reversarlas) para el cálculo de niveles
    // y dibujarlas como aristas de feedback al final.
    enum class Color { Unvisited = 0, OnStack, Done };
    std::unordered_map<int, Color> color;
    for (const auto& n : nodes) color[n.id] = Color::Unvisited;
    std::set<std::pair<int, int>> backEdges;

    // DFS iterativo (evitar stack overflow en grafos profundos).
    for (const auto& n0 : nodes) {
        if (color[n0.id] != Color::Unvisited) continue;
        std::vector<std::pair<int, size_t>> stack;
        stack.emplace_back(n0.id, 0);
        color[n0.id] = Color::OnStack;
        while (!stack.empty()) {
            auto& [u, idx] = stack.back();
            const auto& out = outgoing[u];
            if (idx < out.size()) {
                const int v = out[idx++];
                if (color[v] == Color::Unvisited) {
                    color[v] = Color::OnStack;
                    stack.emplace_back(v, 0);
                } else if (color[v] == Color::OnStack) {
                    backEdges.insert({ u, v });
                }
                // Color::Done: forward/cross edge, sin acción.
            } else {
                color[u] = Color::Done;
                stack.pop_back();
            }
        }
    }

    // In-degree ignorando back-edges → grafo es DAG en este sub-aspecto.
    std::unordered_map<int, int> inDeg;
    for (const auto& n : nodes) inDeg[n.id] = 0;
    for (const auto& e : m_graph->edges()) {
        if (backEdges.count({ e.fromNodeId, e.toNodeId })) continue;
        inDeg[e.toNodeId]++;
    }

    // Kahn estándar sobre el DAG resultante.  Longest-path se logra
    // solo asignando level al cerrar in-degree (todos los predecesores
    // forward ya están procesados).
    std::unordered_map<int, int> level;
    std::queue<int>              q;
    for (const auto& n : nodes) {
        if (n.type == NodeType::SubGraphInput || inDeg[n.id] == 0) {
            level[n.id] = 0;
            q.push(n.id);
        }
    }
    while (!q.empty()) {
        const int u  = q.front(); q.pop();
        const int lv = level[u];
        for (int v : outgoing[u]) {
            if (backEdges.count({ u, v })) continue;  // ignora feedback
            // Tracking provisional del max level previsto (cota inferior).
            auto it = level.find(v);
            if (it == level.end() || it->second < lv + 1)
                level[v] = lv + 1;
            if (--inDeg[v] == 0) q.push(v);
        }
    }
    // Cualquier nodo sin nivel (islas desconectadas): nivel 0.
    for (const auto& n : nodes)
        if (!level.count(n.id)) level[n.id] = 0;

    // ALAP pass para sources (etapa 6I.Q): el Kahn standard pone TODO
    // source en nivel 0 ("ASAP — as soon as possible").  Si el source
    // tiene su único consumer al final del pipeline (Object3D
    // alimentando TransformObject siete niveles después), queda con
    // una arista cruzando todo el grafo.  Pull los sources hacia
    // "min(consumer level) - 1" — quedan pegados a su consumer más
    // cercano sin violar la topología:
    //
    //     ANTES:  Object3D ──────────────────────── TransformObject
    //              (lvl 0)                            (lvl 7)
    //
    //     AHORA:                              Object3D ── TransformObject
    //                                          (lvl 6)     (lvl 7)
    //
    // Sólo aplica a sources (inDeg == 0).  Nodos intermedios mantienen
    // ASAP — su lugar es función de sus predecesores.
    for (const auto& n : nodes) {
        if (n.type == NodeType::SubGraphInput) continue;   // pegado a lvl 0
        if (!incoming[n.id].empty())           continue;   // no es source
        if (outgoing[n.id].empty())            continue;   // huérfano
        int minConsumer = std::numeric_limits<int>::max();
        for (int c : outgoing[n.id]) {
            if (backEdges.count({ n.id, c })) continue;
            auto it = level.find(c);
            if (it != level.end() && it->second < minConsumer)
                minConsumer = it->second;
        }
        if (minConsumer != std::numeric_limits<int>::max()
            && minConsumer - 1 > level[n.id]) {
            level[n.id] = minConsumer - 1;
        }
    }

    // Empujar los SubGraphOutput a la última columna (siempre a la
    // derecha de todo lo demás) para que la dirección de flujo se vea.
    int maxLevel = 0;
    for (const auto& [_, lv] : level) maxLevel = std::max(maxLevel, lv);
    for (const auto& n : nodes)
        if (n.type == NodeType::SubGraphOutput)
            level[n.id] = maxLevel + 1;

    // Agrupar por nivel y ordenar dentro de cada nivel.
    std::map<int, std::vector<int>> byLevel;
    for (const auto& n : nodes)
        byLevel[level[n.id]].push_back(n.id);

    // Barycenter en una pasada (Sugiyama simplificado): cada nodo se
    // posiciona en el promedio del orden de sus predecesores en el
    // nivel anterior; los huérfanos quedan al final estable.
    std::unordered_map<int, int> orderInLevel;
    if (!byLevel.empty()) {
        auto& first = byLevel.begin()->second;
        std::sort(first.begin(), first.end());
        for (size_t k = 0; k < first.size(); ++k)
            orderInLevel[first[k]] = static_cast<int>(k);
    }
    for (auto it = byLevel.begin(); it != byLevel.end(); ++it) {
        if (it == byLevel.begin()) continue;
        auto& ids = it->second;
        std::vector<std::pair<float, int>> ranked;
        ranked.reserve(ids.size());
        for (int id : ids) {
            float sum = 0.f; int count = 0;
            for (int p : incoming[id]) {
                auto it2 = orderInLevel.find(p);
                if (it2 != orderInLevel.end()) {
                    sum += static_cast<float>(it2->second);
                    ++count;
                }
            }
            float bary = (count > 0) ? sum / count
                                     : std::numeric_limits<float>::infinity();
            ranked.emplace_back(bary, id);
        }
        std::sort(ranked.begin(), ranked.end(),
            [&](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first < b.first;
                // Tiebreak: nodos INTERMEDIOS (con sucesores) van
                // antes que SINKS (sin sucesores) en la columna.  Así
                // un nodo como Ganancia (Motor → Ganancia → CombineXYZ)
                // toma el spot superior alineado con su predecesor,
                // y los sinks como Osciloscopio caen debajo.
                const bool aHasOut = !outgoing[a.second].empty();
                const bool bHasOut = !outgoing[b.second].empty();
                if (aHasOut != bHasOut) return aHasOut > bHasOut;
                return a.second < b.second;
            });
        ids.clear();
        for (const auto& [_, id] : ranked) ids.push_back(id);
        for (size_t k = 0; k < ids.size(); ++k)
            orderInLevel[ids[k]] = static_cast<int>(k);
    }

    // Anchura de cada columna = max(width) entre sus nodos + padding.
    constexpr float kColPad  = 80.f;
    constexpr float kRowPad  = 30.f;
    constexpr float kOriginX = 200.f;
    constexpr float kOriginY = 180.f;
    std::map<int, float> colWidth;
    for (const auto& [lv, ids] : byLevel) {
        float w = 0.f;
        for (int id : ids) w = std::max(w, dimensionsOf(id).w);
        colWidth[lv] = w;
    }
    std::map<int, float> colX;
    {
        float x = kOriginX;
        for (const auto& [lv, w] : colWidth) {
            colX[lv] = x;
            x += w + kColPad;
        }
    }

    // Asignar posiciones iniciales.  Apilar verticalmente con altura
    // real para evitar overlap.
    for (const auto& [lv, ids] : byLevel) {
        const float x = colX[lv];
        float y = kOriginY;
        for (int id : ids) {
            const CanvasDims d = dimensionsOf(id);
            m_positions[id] = { x, y };
            y += d.h + kRowPad;
        }
    }

    // Layout físico (etapa 6I.S): sistema de fuerzas en equilibrio
    // estático.  El usuario lo planteó así: "los cables son cuerdas
    // tensionadas, el grafo una red en equilibrio, la fuerza es un
    // vector con magnitud y sentido".
    //
    // Modelo:
    //   - Cada edge = resorte de Hooke entre los dos PINS específicos
    //     (no entre nodos).  Tensión = srcPin.Y - dstPin.Y.
    //   - Newton 3ª: la tensión se distribuye en partes iguales hacia
    //     los dos endpoints (cada uno recibe la MITAD del Δ).
    //   - X está anclado por la columna del topo-sort (Kahn) — la
    //     dirección del flujo no se discute.
    //   - El overlap dentro de columna se resuelve por packing tras
    //     cada iteración (es la única fuerza no-Hookean: contacto).
    //   - SubGraphInput/Output están anclados — son los "muros" del
    //     subgrafo, no participan del equilibrio.
    //
    // Iteramos con damping (factor < 1) para que el sistema converja
    // sin oscilar.  Para grafos típicos (< 50 nodos), 30 iteraciones
    // con damping 0.3 alcanzan equilibrio.
    auto pinYOffset = [](const NodeInstance& n, int port, bool isOutput) -> float {
        const NodeDef& def = defOf(n);
        int row = port;
        if (isOutput) row += def.inputPorts + static_cast<int>(def.params.size());
        return kNodeTitleHeight
             + kNodePadInner
             + row * kNodeRowHeight
             + kNodeRowHeight * 0.5f;
    };
    constexpr int   kForceIters = 30;
    constexpr float kDamping    = 0.3f;
    for (int iter = 0; iter < kForceIters; ++iter) {
        // Acumular fuerza neta en cada nodo desde TODOS los edges.
        std::unordered_map<int, float> fy;
        for (const auto& n : nodes) fy[n.id] = 0.f;
        for (const auto& e : m_graph->edges()) {
            if (backEdges.count({ e.fromNodeId, e.toNodeId })) continue;
            const NodeInstance* src = m_graph->findNode(e.fromNodeId);
            const NodeInstance* dst = m_graph->findNode(e.toNodeId);
            if (!src || !dst) continue;
            const int srcPort = attrOutputPort(e.fromAttrId);
            const int dstPort = attrInputPort(e.toAttrId);
            const float srcPinY = m_positions[e.fromNodeId].y
                                + pinYOffset(*src, srcPort, /*isOutput=*/true);
            const float dstPinY = m_positions[e.toNodeId].y
                                + pinYOffset(*dst, dstPort, /*isOutput=*/false);
            // Tensión: si dst está debajo de src, la cuerda tira a
            // src abajo y a dst arriba (signos opuestos, mismo módulo).
            const float tension = srcPinY - dstPinY;
            fy[e.fromNodeId] -= tension * 0.5f;   // src hacia abajo
            fy[e.toNodeId]   += tension * 0.5f;   // dst hacia arriba
        }
        // Aplicar la fuerza (damped) a los nodos no-anclados.
        for (const auto& n : nodes) {
            if (n.type == NodeType::SubGraphInput ||
                n.type == NodeType::SubGraphOutput) continue;
            m_positions[n.id].y += fy[n.id] * kDamping;
        }
        // Resolver overlap dentro de cada columna (contacto = repulsión
        // dura): ordenar por Y actual y empujar a los siguientes.
        for (auto& [lv, ids] : byLevel) {
            std::sort(ids.begin(), ids.end(),
                      [&](int a, int b) {
                          return m_positions[a].y < m_positions[b].y;
                      });
            float yLimit = -std::numeric_limits<float>::infinity();
            for (int id : ids) {
                if (m_positions[id].y < yLimit)
                    m_positions[id].y = yLimit;
                yLimit = m_positions[id].y + dimensionsOf(id).h + kRowPad;
            }
        }
    }
}

}  // namespace scinodes::ui
