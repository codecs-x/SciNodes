#include "NodeGraph.hpp"

#include "DimensionalAnalyzer.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
    for (int oldId : ids) {
        const NodeInstance* src = findNode(oldId);
        if (!src) continue;
        int newId;
        if (src->type == NodeType::Custom)
            newId = child->addCustomNodeWithId(src->customType, oldId);
        else if (isSubGraphContainer(src->type)) {
            // SubGraph anidado: tomar el grafo hijo del padre original
            // y reinstalarlo bajo el mismo ID en el nuevo nivel.
            newId = child->addNodeWithId(NodeType::SubGraph, oldId);
            if (auto* nested = subGraphOf(oldId)) {
                NodeGraph copy = *nested;
                child->installSubGraph(newId, std::move(copy));
            }
            child->recomputeSubGraphPorts(newId);
        }
        else newId = child->addNodeWithId(src->type, oldId);
        idMap[oldId] = newId;
        for (const auto& [k, v] : src->params)       child->setParam(newId, k, v);
        for (const auto& [k, v] : src->stringParams) child->setStringParam(newId, k, v);
        if (!src->assetPath.empty())                 child->setAssetPath(newId, src->assetPath);
    }

    // Aristas internas.
    for (const Edge& e : internalEdges) {
        auto fIt = idMap.find(e.fromNodeId);
        auto tIt = idMap.find(e.toNodeId);
        if (fIt == idMap.end() || tIt == idMap.end()) continue;
        child->tryAddEdge(attrRemap(e.fromAttrId, fIt->second),
                          attrRemap(e.toAttrId,   tIt->second));
    }

    // Stubs primero (todos), luego recompute, luego cablear.
    std::vector<int> inStubIds, outStubIds;
    for (size_t k = 0; k < extInSources.size(); ++k) {
        int stubId = child->addNode(NodeType::SubGraphInput);
        child->setParam(stubId, "Port", double(k));
        inStubIds.push_back(stubId);
    }
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        int stubId = child->addNode(NodeType::SubGraphOutput);
        child->setParam(stubId, "Port", double(k));
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
    for (int oldId : ids) removeNode(oldId);

    // Crear aristas externas nuevas hacia/desde el SubGraph.
    for (size_t k = 0; k < extInSources.size(); ++k)
        tryAddEdge(extInSources[k], sgId * kAttrIdNodeStride + int(k));
    for (size_t k = 0; k < mappedOutSrc.size(); ++k) {
        int sgOutAttr = sgId * kAttrIdNodeStride + kAttrIdOutputBase + int(k);
        for (int toAttr : outConsumers[k])
            tryAddEdge(sgOutAttr, toAttr);
    }
    return result;
}

void NodeGraph::installSubGraph(int subGraphNodeId, NodeGraph&& child) {
    m_subGraphs[subGraphNodeId] =
        std::make_shared<NodeGraph>(std::move(child));
}

void NodeGraph::recomputeSubGraphPorts(int subGraphNodeId) {
    auto it = m_subGraphs.find(subGraphNodeId);
    if (it == m_subGraphs.end() || !it->second) return;
    int nIn = 0, nOut = 0;
    for (const NodeInstance& c : it->second->nodes()) {
        if (c.type == NodeType::SubGraphInput)  ++nIn;
        if (c.type == NodeType::SubGraphOutput) ++nOut;
    }
    for (NodeInstance& n : m_nodes) {
        if (n.id == subGraphNodeId && isSubGraphContainer(n.type)) {
            n.subGraphInputCount  = nIn;
            n.subGraphOutputCount = nOut;
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
    // dimensional, ver `doc/dimensional_analysis_proposal.md` v2 §5).
    //
    // Estrategia pre/post: corremos analyzeUnits ANTES y DESPUÉS de
    // añadir tentativamente el edge.  Si la cantidad de conflictos
    // crece, el edge es responsable de al menos uno — lo identificamos
    // y rechazamos con R7.
    //
    // El toggle `m_dimEnforce` permite desactivar el check (default
    // ON; legacy tests / .scn deserialize lo apagan).
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
