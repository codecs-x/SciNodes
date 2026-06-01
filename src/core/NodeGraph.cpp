#include "NodeGraph.hpp"

#include "DimensionalAnalyzer.hpp"
#include "NodeKind.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

NodeGraph::NodeGraph() = default;

NodeGraph::NodeGraph(const NodeGraph& other)
    : m_nodes      (other.m_nodes),
      m_edges      (other.m_edges),
      m_nextNodeId (other.m_nextNodeId),
      m_nextEdgeId (other.m_nextEdgeId),
      m_id         (other.m_id),
      m_title      (other.m_title),
      m_description(other.m_description),
      m_tags       (other.m_tags),
      m_objects    (other.m_objects),
      m_parser     ()
{
    // Deep clone of child subgraphs so snapshots/undo are independent
    // of the live graph.  Defaulted copy would share the shared_ptr.
    for (const auto& [k, v] : other.m_subGraphs) {
        if (v) m_subGraphs[k] = std::make_shared<NodeGraph>(*v);
    }
}

NodeGraph& NodeGraph::operator=(const NodeGraph& other) {
    if (this == &other) return *this;
    NodeGraph tmp(other);
    using std::swap;
    swap(m_nodes,       tmp.m_nodes);
    swap(m_edges,       tmp.m_edges);
    swap(m_nextNodeId,  tmp.m_nextNodeId);
    swap(m_nextEdgeId,  tmp.m_nextEdgeId);
    swap(m_id,          tmp.m_id);
    swap(m_title,       tmp.m_title);
    swap(m_description, tmp.m_description);
    swap(m_tags,        tmp.m_tags);
    swap(m_objects,     tmp.m_objects);
    swap(m_subGraphs,   tmp.m_subGraphs);
    return *this;
}

void NodeGraph::setId         (std::string s)              { m_id          = std::move(s); }
void NodeGraph::setTitle      (std::string s)              { m_title       = std::move(s); }
void NodeGraph::setDescription(std::string s)              { m_description = std::move(s); }
void NodeGraph::setTags       (std::vector<std::string> v) { m_tags        = std::move(v); }

void NodeGraph::addImportedObject(ImportedObject o) {
    // Si ya existe un objeto con el mismo nombre, lo reemplaza
    // (re-import del mismo .gltf actualiza ruta y partes).
    for (auto& existing : m_objects)
        if (existing.name == o.name) { existing = std::move(o); return; }
    m_objects.push_back(std::move(o));
}
void NodeGraph::removeImportedObject(const std::string& name) {
    m_objects.erase(
        std::remove_if(m_objects.begin(), m_objects.end(),
            [&name](const ImportedObject& o) { return o.name == name; }),
        m_objects.end());
}
void NodeGraph::setImportedObjects(std::vector<ImportedObject> v) {
    m_objects = std::move(v);
}
const ImportedObject* NodeGraph::findImportedObject(const std::string& name) const {
    for (const auto& o : m_objects)
        if (o.name == name) return &o;
    return nullptr;
}

// ---------------------------------------------------------------------------
// node management
// ---------------------------------------------------------------------------
int NodeGraph::addNode(NodeType type) {
    int id = m_nextNodeId++;
    m_nodes.push_back(makeNode(id, type));
    return id;
}

int NodeGraph::addNodeWithId(NodeType type, int id) {
    m_nodes.push_back(makeNode(id, type));
    if (id >= m_nextNodeId) m_nextNodeId = id + 1;
    return id;
}

int NodeGraph::addCustomNodeWithId(const std::string& customType, int id) {
    m_nodes.push_back(makeCustomNode(id, customType));
    if (id >= m_nextNodeId) m_nextNodeId = id + 1;
    return id;
}

int NodeGraph::addNodeMirroring(const NodeInstance& src,
                                int preferredId,
                                const NodeGraph* srcContainer)
{
    // Decidir si reservamos el preferredId o asignamos uno fresco —
    // mismo predicado para todas las kinds.
    const bool useId = (preferredId > 0) && (findNode(preferredId) == nullptr);

    // std::visit sobre el sum-type de NodeKind: cada producción sabe qué
    // factory llamar.  Sin if/switch sobre `src.type` en el caller.
    return std::visit(
        [&](const auto& kind) -> int {
            using K = std::decay_t<decltype(kind)>;
            if constexpr (std::is_same_v<K, scinodes::CustomKind>) {
                return useId ? addCustomNodeWithId(src.customType, preferredId)
                             : addCustomNode(src.customType);
            } else if constexpr (std::is_same_v<K, scinodes::SubGraphContainerKind>) {
                // Reservamos el id sin pasar por addSubGraphNode (que
                // crearía stubs default que pisaríamos al instalar el
                // child).  El llamador puede pasar `srcContainer` para
                // copiar el grafo hijo y re-sincronizar los port counts.
                int newId = useId
                              ? addNodeWithId(NodeType::SubGraph, preferredId)
                              : addNode(NodeType::SubGraph);
                if (srcContainer) {
                    if (const NodeGraph* csub = srcContainer->subGraphOf(src.id)) {
                        installSubGraph(newId, NodeGraph(*csub));
                        recomputeSubGraphPorts(newId);
                    }
                }
                return newId;
            } else {
                // BuiltinKind, SubGraphInputKind, SubGraphOutputKind —
                // todos usan la factory plana del NodeType.
                return useId ? addNodeWithId(src.type, preferredId)
                             : addNode(src.type);
            }
        },
        scinodes::kindOf(src));
}

int NodeGraph::addSubGraphNode() {
    const int id = addNode(NodeType::SubGraph);
    // Inicializar grafo hijo con un input y un output stubs por defecto.
    auto child = std::make_shared<NodeGraph>();
    int inId  = child->addNode(NodeType::SubGraphInput);
    int outId = child->addNode(NodeType::SubGraphOutput);
    child->setParam(inId,  "Port", 0.0);
    child->setParam(outId, "Port", 0.0);
    m_subGraphs[id] = std::move(child);
    recomputeSubGraphPorts(id);
    return id;
}

NodeGraph* NodeGraph::subGraphOf(int nodeId) {
    auto it = m_subGraphs.find(nodeId);
    return (it != m_subGraphs.end()) ? it->second.get() : nullptr;
}
const NodeGraph* NodeGraph::subGraphOf(int nodeId) const {
    auto it = m_subGraphs.find(nodeId);
    return (it != m_subGraphs.end()) ? it->second.get() : nullptr;
}

NodeGraph::EncapsulateResult
NodeGraph::encapsulateByIds(const std::vector<int>& ids) {
    EncapsulateResult result;
    if (ids.empty()) return result;
    std::unordered_set<int> selSet(ids.begin(), ids.end());

    // Rechazar si la selección contiene port stubs.
    for (int id : ids) {
        if (const auto* n = findNode(id)) {
            if (isSubGraphStub(n->type))
                return result;   // sgId = 0
        }
    }
    // Esta check usa la selección ORIGINAL (no la ajustada por alias)
    // porque el usuario es quien explicitamente pone stubs en la sel.

    // Etapa 6I.U.c: Aliases referencian otros nodos por id, sin edge
    // visible — ajustamos la selección para que un Alias y su target
    // queden SIEMPRE en el mismo grafo después de encapsular.  Sin
    // esto, el alias quedaba con target apuntando a un id que el
    // grafo actual ya no contiene (mostrando "sin asignar").
    //
    // Reglas:
    //   - target seleccionado + alias NO seleccionado → incluir alias.
    //   - alias seleccionado + target NO seleccionado → excluir alias.
    //
    // Iteramos hasta fixed-point por si hay cadenas alias→alias
    // (aunque la UI las desalienta).
    bool changed = true;
    int  guard   = 16;
    while (changed && guard-- > 0) {
        changed = false;
        for (const NodeInstance& n : m_nodes) {
            if (n.type != NodeType::Alias) continue;
            auto pIt = n.params.find("target_node_id");
            if (pIt == n.params.end()) continue;
            const int tid = static_cast<int>(pIt->second);
            if (tid <= 0) continue;
            const bool aliasSel  = selSet.count(n.id)  > 0;
            const bool targetSel = selSet.count(tid)   > 0;
            if (targetSel && !aliasSel) {
                selSet.insert(n.id);
                changed = true;
            } else if (aliasSel && !targetSel) {
                selSet.erase(n.id);
                changed = true;
            }
        }
    }
    // Reconstruir ids con el orden estable de m_nodes para que el
    // resto del algoritmo (que itera ids) procese deterministically.
    std::vector<int> adjustedIds;
    adjustedIds.reserve(selSet.size());
    for (const NodeInstance& n : m_nodes)
        if (selSet.count(n.id)) adjustedIds.push_back(n.id);
    const std::vector<int>& workingIds = adjustedIds;

    // Particionar aristas en internas/entrantes/salientes.
    std::vector<Edge> internalEdges, inEdges, outEdges;
    for (const Edge& e : m_edges) {
        bool f = selSet.count(e.fromNodeId) > 0;
        bool t = selSet.count(e.toNodeId)   > 0;
        if (f && t)         internalEdges.push_back(e);
        else if (!f && t)   inEdges.push_back(e);
        else if (f && !t)   outEdges.push_back(e);
    }

    std::vector<int> extInSources, mappedInTargetAttr;
    for (const Edge& e : inEdges) {
        extInSources.push_back(e.fromAttrId);
        mappedInTargetAttr.push_back(e.toAttrId);
    }
    std::vector<std::pair<int,int>> mappedOutSrc;
    std::vector<std::vector<int>>   outConsumers;
    {
        std::map<std::pair<int,int>, int> grouper;
        for (const Edge& e : outEdges) {
            int fromPort = attrOutputPort(e.fromAttrId);
            auto key = std::make_pair(e.fromNodeId, fromPort);
            auto [it, ins] = grouper.try_emplace(key,
                static_cast<int>(mappedOutSrc.size()));
            if (ins) { mappedOutSrc.push_back(key);
                       outConsumers.emplace_back(); }
            outConsumers[it->second].push_back(e.toAttrId);
        }
    }

    // Etapa 6I.V — capturar el TypeExpr de cada puerto cruzando la
    // frontera ANTES de crear el SubGraph y borrar los nodos viejos.
    // El stub hereda el tipo del puerto que envuelve para que R6 no
    // silencie cables Geometry / vec(N) al re-cablear.
    std::vector<TypeExpr> extInTypes;  extInTypes.reserve(extInSources.size());
    for (int srcAttr : extInSources) {
        const NodeInstance* src = findNode(attrNodeId(srcAttr));
        extInTypes.push_back(
            src ? outputPortTypeOf(defOf(*src), attrOutputPort(srcAttr))
                : exprScalar());
    }
    std::vector<TypeExpr> outSrcTypes; outSrcTypes.reserve(mappedOutSrc.size());
    for (const auto& [origNodeId, origPort] : mappedOutSrc) {
        const NodeInstance* src = findNode(origNodeId);
        outSrcTypes.push_back(
            src ? outputPortTypeOf(defOf(*src), origPort) : exprScalar());
    }

    // Crear el SubGraph y limpiar sus stubs default.
    const int sgId = addSubGraphNode();
    NodeGraph* child = subGraphOf(sgId);
    if (!child) return result;
    result.sgId = sgId;
    for (const NodeInstance& s : std::vector<NodeInstance>(child->nodes())) {
        if (isSubGraphStub(s.type)) child->removeNode(s.id);
    }

    // Copiar nodos seleccionados al child PRESERVANDO sus IDs.  Sin
    // esto, encapsular renumera los nodos internos y un Resume tras
    // la operación pierde todo el estado acumulado: el seed devuelto
    // por captureState está indexado por (oldId, slot), pero el plan
    // generado del nuevo grafo busca (newId, slot) → no encuentra
    // nada → cae a las IC default → la respuesta empieza desde cero
    // en t actual (efecto "función escalón corrida").
    auto& idMap = result.idMap;
    for (int oldId : workingIds) {
        const NodeInstance* src = findNode(oldId);
        if (!src) continue;
        // Etapa 6J.6: dispatch sobre Custom/SubGraph/Builtin centralizada
        // en `addNodeMirroring`.  Pasamos `this` como `srcContainer` para
        // que la rama SubGraph copie el grafo hijo nested del padre.
        const int newId = child->addNodeMirroring(*src, oldId, this);
        idMap[oldId] = newId;
        for (const auto& [k, v] : src->params)       child->setParam(newId, k, v);
        for (const auto& [k, v] : src->stringParams) child->setStringParam(newId, k, v);
        if (!src->assetPath.empty())                 child->setAssetPath(newId, src->assetPath);
        // Transferir overrides de unidad per-puerto (etapa 6G).  Sin
        // esto, un PID con override "in=rad/s, out=V" pierde el
        // override al encapsularse y el child queda con PID polimórfico,
        // que con R7 ON genera conflict en el feedback.
        for (const auto& [key, text] : src->portUnitOverrides)
            child->setPortUnitOverride(newId, key, text);
    }

    // Aristas internas — migración, no validación.  Las aristas
    // originales ya pasaron R7 en el momento de tryAddEdge inicial;
    // re-evaluarlas en el child puede generar falsos rechazos por
    // orden de propagación distinto (los nodos llegan en otro orden,
    // las units se siembran en otro orden, y un edge que era
    // dimensionalmente válido al final del fixed-point puede parecer
    // inválido en pasos intermedios).  Bypassemos R7 durante la
    // copia y restauramos al terminar.
    const bool savedChildEnforce = child->isDimensionalEnforcementOn();
    child->setDimensionalEnforcement(false);
    for (const Edge& e : internalEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        child->tryAddEdge(attrRemap(e.fromAttrId, fIt->second),
                          attrRemap(e.toAttrId,   tIt->second));
    }
    child->setDimensionalEnforcement(savedChildEnforce);

    // Stubs primero (todos), luego recompute, luego cablear.
    std::vector<int> inStubIds, outStubIds;
    for (size_t k = 0; k < extInSources.size(); ++k) {
        int stubId = child->addNode(NodeType::SubGraphInput);
        child->setParam(stubId, "Port", double(k));
        // Etapa 6I.V — el output del stub hereda el tipo del puerto
        // externo que lo alimenta.  Sin esto el stub default es escalar
        // y un cable Geometry adentro del subgrafo falla R6 silenciosamente.
        child->setStubPortType(stubId, extInTypes[k]);
        inStubIds.push_back(stubId);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        int stubId = child->addNode(NodeType::SubGraphOutput);
        child->setParam(stubId, "Port", double(k));
        // Idem para outputs: el input del stub hereda el tipo del puerto
        // interno que emite hacia él.
        child->setStubPortType(stubId, outSrcTypes[k]);
        outStubIds.push_back(stubId);
    }
    recomputeSubGraphPorts(sgId);

    // Cablear stubs internamente.
    for (size_t k = 0; k < extInSources.size(); ++k) {
        int origTarget = mappedInTargetAttr[k];
        auto tIt = idMap.find(attrNodeId(origTarget));
        if (tIt == idMap.end()) continue;
        int newTarget = attrRemap(origTarget, tIt->second);
        // El stub SubGraphInput emite por su puerto de salida 0.
        child->tryAddEdge(inStubIds[k] * kAttrIdNodeStride + kAttrIdOutputBase,
                          newTarget);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        auto [origNodeId, origPort] = mappedOutSrc[k];
        auto fIt = idMap.find(origNodeId);
        if (fIt == idMap.end()) continue;
        int newSource = fIt->second * kAttrIdNodeStride + kAttrIdOutputBase + origPort;
        // El stub SubGraphOutput recibe por su input 0.
        child->tryAddEdge(newSource, outStubIds[k] * kAttrIdNodeStride);
    }

    // Borrar nodos viejos (limpia aristas externas viejas).
    for (int oldId : workingIds) removeNode(oldId);

    // Crear aristas externas nuevas hacia/desde el SubGraph.  Igual
    // que las internas: bypasseamos R7 porque estas aristas son la
    // restitución de cables que YA pasaron R7 en su forma original;
    // su rechazo en el momento de re-cablear es falso negativo
    // (las unidades del SubGraph stub heredan del puerto interno
    // pero el orden de seed/propagación cambia).
    const bool savedParentEnforce = isDimensionalEnforcementOn();
    setDimensionalEnforcement(false);
    for (size_t k = 0; k < extInSources.size(); ++k)
        tryAddEdge(extInSources[k], sgId * kAttrIdNodeStride + int(k));
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        int sgOutAttr = sgId * kAttrIdNodeStride + kAttrIdOutputBase + int(k);
        for (int toAttr : outConsumers[k])
            tryAddEdge(sgOutAttr, toAttr);
    }
    setDimensionalEnforcement(savedParentEnforce);
    return result;
}

void NodeGraph::installSubGraph(int subGraphNodeId, NodeGraph&& child) {
    m_subGraphs[subGraphNodeId] =
        std::make_shared<NodeGraph>(std::move(child));
}

void NodeGraph::setStubPortType(int stubNodeId, const TypeExpr& portType) {
    for (NodeInstance& n : m_nodes) {
        if (n.id != stubNodeId) continue;
        // SubGraphInput tiene 1 output (port 0); SubGraphOutput tiene 1
        // input (port 0).  La clave usa el mismo esquema que
        // portUnitOverrides.  Un dispatch sutil queda — pero acotado a
        // este setter, no propagado por el código.
        const int key = (n.type == NodeType::SubGraphInput)
                            ? portKeyForOutput(0)
                            : portKeyForInput(0);
        n.portTypeOverrides[key] = portType;
        return;
    }
}

void NodeGraph::recomputeSubGraphPorts(int subGraphNodeId) {
    auto it = m_subGraphs.find(subGraphNodeId);
    if (it == m_subGraphs.end() || !it->second) return;
    // Indexar stubs por su parámetro `Port` — el orden exterior del
    // contenedor coincide con el del stub aunque los stubs vivan en
    // cualquier orden dentro del grafo hijo.
    //
    // Para cada stub leemos su override (mismo storage que
    // portUnitOverrides; ver NodeInstance.hpp::portTypeOverrides).
    auto stubPortAndType = [](const NodeInstance& c) {
        int port = 0;
        if (auto pIt = c.params.find("Port"); pIt != c.params.end())
            port = static_cast<int>(pIt->second);
        const int key = (c.type == NodeType::SubGraphInput)
                            ? portKeyForOutput(0) : portKeyForInput(0);
        TypeExpr t = exprScalar();
        if (auto tIt = c.portTypeOverrides.find(key); tIt != c.portTypeOverrides.end())
            t = tIt->second;
        return std::pair{ port, t };
    };
    std::map<int, TypeExpr> inTypeByPort, outTypeByPort;
    int nIn = 0, nOut = 0;
    for (const NodeInstance& c : it->second->nodes()) {
        if (c.type == NodeType::SubGraphInput) {
            ++nIn;
            auto [port, t] = stubPortAndType(c);
            inTypeByPort[port] = t;
        } else if (c.type == NodeType::SubGraphOutput) {
            ++nOut;
            auto [port, t] = stubPortAndType(c);
            outTypeByPort[port] = t;
        }
    }
    for (NodeInstance& n : m_nodes) {
        if (n.id == subGraphNodeId && isSubGraphContainer(n.type)) {
            n.subGraphInputCount  = nIn;
            n.subGraphOutputCount = nOut;
            // Reconstruir overrides en el mismo storage unificado.
            // Limpiar primero las entradas viejas; un puerto que ya no
            // tiene stub no debe dejar el override colgado.
            n.portTypeOverrides.clear();
            for (int p = 0; p < nIn; ++p) {
                auto tIt = inTypeByPort.find(p);
                if (tIt != inTypeByPort.end() && !isScalarType(tIt->second))
                    n.portTypeOverrides[portKeyForInput(p)] = tIt->second;
            }
            for (int p = 0; p < nOut; ++p) {
                auto tIt = outTypeByPort.find(p);
                if (tIt != outTypeByPort.end() && !isScalarType(tIt->second))
                    n.portTypeOverrides[portKeyForOutput(p)] = tIt->second;
            }
            break;
        }
    }
}

int NodeGraph::addCustomNode(const std::string& customType) {
    int id = m_nextNodeId++;
    m_nodes.push_back(makeCustomNode(id, customType));
    return id;
}

void NodeGraph::removeNode(int nodeId) {
    // Remove all edges incident to this node first
    m_edges.erase(
        std::remove_if(m_edges.begin(), m_edges.end(),
            [nodeId](const Edge& e) {
                return e.fromNodeId == nodeId || e.toNodeId == nodeId;
            }),
        m_edges.end());

    m_nodes.erase(
        std::remove_if(m_nodes.begin(), m_nodes.end(),
            [nodeId](const NodeInstance& n) { return n.id == nodeId; }),
        m_nodes.end());

    // Drop the child graph if this was a SubGraph node.
    m_subGraphs.erase(nodeId);
}

// ---------------------------------------------------------------------------
// edge management
// ---------------------------------------------------------------------------
std::optional<GrammarError>
NodeGraph::tryAddEdge(int fromAttrId, int toAttrId) {
    // Normalise: la convención es que el primer arg sea el OUTPUT y el
    // segundo el INPUT o PARAM.  Si el caller los pasó al revés
    // (un output como toAttr), intercambiamos.
    if (!attrIsOutput(fromAttrId)) std::swap(fromAttrId, toAttrId);

    int fromNodeId = attrNodeId(fromAttrId);
    int toNodeId   = attrNodeId(toAttrId);

    const NodeInstance* from = findNode(fromNodeId);
    const NodeInstance* to   = findNode(toNodeId);
    if (!from || !to)
        return GrammarError{"R0", "Unknown node in connection.", fromNodeId, toNodeId};

    // R4 (port-aware duplicate). The grammar parser only knows about the
    // node-pair level; here we have the proposed attribute pair, so
    // forbid only the genuinely-duplicate (fromAttrId, toAttrId) pair.
    // Multi-output sources are free to drive multiple inputs of the same
    // destination as long as each wire lands on a distinct (port, port).
    for (const auto& e : m_edges)
        if (e.fromAttrId == fromAttrId && e.toAttrId == toAttrId)
            return GrammarError{"R4",
                "This connection already exists.",
                fromNodeId, toNodeId};

    const bool toIsParam = attrIsParam(toAttrId);
    // Port indices para R6 (sub-lenguaje Signal/Geometry).  Para los
    // outputs decodificamos el índice de puerto desde el attrId; para
    // params, pasamos el índice del param — `validateEdge` lo trata
    // como Signal (params son siempre escalares) y R6 sólo necesita la
    // resolución del lado FROM.
    const int fromPortIdx = attrOutputPort(fromAttrId);
    const int toPortIdx   = toIsParam ? attrParamIdx(toAttrId)
                                       : attrInputPort(toAttrId);
    auto err = m_parser.validateEdge(*from, *to, m_edges, toIsParam,
                                     fromPortIdx, toPortIdx);
    if (err) return err;

    // Check that the specific input port (toAttrId) is not already occupied.
    // GrammarParser only checks whether *all* ports are full; this catches
    // the case where two wires try to land on the same port of a multi-input node.
    for (const auto& e : m_edges)
        if (e.toAttrId == toAttrId)
            return GrammarError{"R5",
                std::string("That input port of \"")
                    + defOf(*to).label
                    + "\" is already connected.",
                fromNodeId, toNodeId};

    // ---- R7 — dimensional consistency (etapa 6F del análisis ----------
    // dimensional, ver `doc/designs/dimensional_analysis_proposal.md` v2 §5).
    //
    // Estrategia pre/post: corremos analyzeUnits ANTES y DESPUÉS de
    // añadir tentativamente el edge.  Si la cantidad de conflictos
    // crece, el edge es responsable de al menos uno — lo identificamos
    // y rechazamos con R7.
    //
    // El toggle `m_dimEnforce` permite desactivar el check (default
    // ON desde v0.1.1; legacy tests lo apagan via
    // setDimensionalEnforcement(false)).
    //
    // Coste: dos pasadas de analyzeUnits por cada tryAddEdge.  Para
    // grafos pedagógicos (<200 edges) es trivial; optimizar a
    // incremental sólo si surge.
    if (m_dimEnforce) {
        const auto pre = scinodes::analyzeUnits(*this);
        m_edges.push_back({ -1, fromNodeId, toNodeId, fromAttrId, toAttrId });
        const auto post = scinodes::analyzeUnits(*this);
        m_edges.pop_back();   // reverso unconditional — si aceptamos,
                              // el push oficial pasa abajo.

        if (post.conflicts.size() > pre.conflicts.size()) {
            // Encuentra el primer conflicto que NO estaba en pre.
            auto wasInPre = [&](const scinodes::DimensionalAnalysis::Conflict& c) {
                for (const auto& p : pre.conflicts) {
                    if (p.attrId == c.attrId && p.message == c.message) return true;
                }
                return false;
            };
            std::string msg = "Dimensional inconsistency.";
            for (const auto& c : post.conflicts) {
                if (!wasInPre(c)) { msg = c.message; break; }
            }
            return GrammarError{ "R7", msg, fromNodeId, toNodeId };
        }
    }

    m_edges.push_back({ m_nextEdgeId++, fromNodeId, toNodeId, fromAttrId, toAttrId });
    return std::nullopt;
}

void NodeGraph::removeEdge(int edgeId) {
    m_edges.erase(
        std::remove_if(m_edges.begin(), m_edges.end(),
            [edgeId](const Edge& e) { return e.id == edgeId; }),
        m_edges.end());
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------
const NodeInstance* NodeGraph::findNode(int nodeId) const {
    for (const auto& n : m_nodes)
        if (n.id == nodeId) return &n;
    return nullptr;
}

void NodeGraph::setStringParam(int nodeId, const std::string& key,
                               const std::string& value) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) {
            n.stringParams[key] = value;
            return;
        }
    }
}

void NodeGraph::setParam(int nodeId, const std::string& name, double value) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) {
            auto it = n.params.find(name);
            if (it != n.params.end())
                it->second = value;
            // Etapa 6I.D.1: espejo a la tabla unificada.  La unidad la
            // preserva la entrada existente (sembrada en makeNode desde
            // FieldDef.defaultQuantity.unit); sólo actualizamos el value.
            auto fit = n.fields.find(name);
            if (fit != n.fields.end())
                fit->second.value = value;
            return;
        }
    }
}

void NodeGraph::setFieldQuantity(int nodeId, const std::string& name,
                                 scinodes::Quantity q) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) {
            n.fields[name] = q;
            // Legacy mirror — sólo si la entrada existía en params; no
            // creamos params para puertos (params es estrictamente
            // params en el modelo legacy).  La GUI del QuantityField
            // setea para fields de tipo Parameter; los de tipo
            // Input/Output sólo modifican fields.
            auto it = n.params.find(name);
            if (it != n.params.end())
                it->second = q.value;
            return;
        }
    }
}

void NodeGraph::setAssetPath(int nodeId, const std::string& path) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) { n.assetPath = path; return; }
    }
}

void NodeGraph::setComment(int nodeId, const std::string& text) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) { n.comment = text; return; }
    }
}

void NodeGraph::setPortUnitOverride(int nodeId, int key, std::string text) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) { n.portUnitOverrides[key] = std::move(text); return; }
    }
}

void NodeGraph::clearPortUnitOverride(int nodeId, int key) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) { n.portUnitOverrides.erase(key); return; }
    }
}

// --- Display units del proyecto (etapa 6I.C) ---------------------------
void NodeGraph::setDisplayUnit(scinodes::Unit u) {
    // Indexamos por la dim signature; reemplaza cualquier preferencia
    // previa para esa dimensión (sólo una unidad preferida por dim).
    m_displayUnits[u.exp] = u;
}

void NodeGraph::clearDisplayUnit(DimensionKey dim) {
    m_displayUnits.erase(dim);
}

scinodes::Quantity NodeGraph::canonicalizeForDisplay(scinodes::Quantity q) const {
    auto it = m_displayUnits.find(q.unit.exp);
    if (it == m_displayUnits.end()) return q;            // sin preferencia
    const scinodes::Unit& pref = it->second;
    // Si la preferencia coincide exactamente con la unidad actual,
    // evitamos la conversión (idempotente).  Mejora estabilidad
    // numérica en re-displays y simplifica el contrato de tests.
    if (q.unit == pref) return q;
    scinodes::Quantity r;
    r.value = q.value * (q.unit.magnitude / pref.magnitude);
    r.unit  = pref;
    return r;
}

const Edge* NodeGraph::findEdge(int edgeId) const {
    for (const auto& e : m_edges)
        if (e.id == edgeId) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// grammar
// ---------------------------------------------------------------------------
GrammarState NodeGraph::grammarState() const {
    // Pasamos *this para que el parser pueda recursar en cada SubGraph.
    // Sin la recursión, un SubGraph internamente roto (p. ej. su output
    // stub no recibe ninguna señal) pasaría como Valid desde el padre y
    // la simulación arrancaría con salidas a 0.0.
    return m_parser.validateGraph(*this);
}

const char* NodeGraph::grammarLabel() const {
    return GrammarParser::label(grammarState());
}

// ---------------------------------------------------------------------------
// snapshot / restore
// ---------------------------------------------------------------------------
GraphSnapshot NodeGraph::snapshot() const {
    GraphSnapshot s;
    s.nodes       = m_nodes;
    s.edges       = m_edges;
    s.nextNodeId  = m_nextNodeId;
    s.nextEdgeId  = m_nextEdgeId;
    s.id          = m_id;
    s.title       = m_title;
    s.description = m_description;
    s.tags        = m_tags;
    // Recursive snapshot of child SubGraphs.
    for (const auto& [k, v] : m_subGraphs) {
        if (v) s.subGraphs[k] = std::make_shared<GraphSnapshot>(v->snapshot());
    }
    return s;
}

void NodeGraph::restoreSnapshot(const GraphSnapshot& s) {
    m_nodes       = s.nodes;
    m_edges       = s.edges;
    m_nextNodeId  = s.nextNodeId;
    m_nextEdgeId  = s.nextEdgeId;
    m_id          = s.id;
    m_title       = s.title;
    m_description = s.description;
    m_tags        = s.tags;
    // Restore child SubGraphs (clean slate then repopulate).
    m_subGraphs.clear();
    for (const auto& [k, v] : s.subGraphs) {
        if (!v) continue;
        auto child = std::make_shared<NodeGraph>();
        child->restoreSnapshot(*v);
        m_subGraphs[k] = std::move(child);
    }
}
