#include "SceneCollector.hpp"
#include "NodeInstance.hpp"

#include <cmath>
#include <optional>
#include <unordered_set>

namespace scinodes {

namespace {

// Convención de stringParams para Object3D — "objectRef" lleva la
// referencia al catálogo en formato "<objectName>" o "<objectName>/<partName>".
// Centralizada acá para que el split (si lleva slash) sea consistente
// con quien escriba el campo desde la UI.
constexpr const char* kObjectRefKey = "objectRef";

// Divide "<name>" o "<name>/<part>" en (name, part).  Si no hay slash,
// part queda vacío y el renderable apunta al asset completo.
void splitObjectRef(const std::string& ref,
                    std::string& outName,
                    std::string& outPart) {
    auto slash = ref.find('/');
    if (slash == std::string::npos) {
        outName = ref;
        outPart.clear();
    } else {
        outName = ref.substr(0, slash);
        outPart = ref.substr(slash + 1);
    }
}

// Devuelve la primera arista que termina en (toNodeId, toPort).  Si
// nada conecta ahí, retorna nullptr.  Las aristas se decoden por su
// attrId — confiamos en attrInputPort para extraer el índice.
const Edge* incomingTo(const NodeGraph& g, int toNodeId, int toPort) {
    for (const auto& e : g.edges()) {
        if (e.toNodeId != toNodeId) continue;
        if (attrInputPort(e.toAttrId) != toPort) continue;
        return &e;
    }
    return nullptr;
}

// Lee el último sample en vivo asociado al output (sourceNodeId,
// sourcePort).  El bridge sólo guarda buffers para nodos Sink; el
// walker busca el primer Sink que comparta el mismo source y devuelve
// su `back()`.  Sin sink "tap" o sin samples, retorna nullopt — el
// caller mantiene el default (identidad).
//
// Convención clave: para animar TransformObject, el usuario cablea
// la señal a (al menos) un Sink — un Oscilloscope o View3DSink.  El
// Sink no necesita ser parte del sub-grafo Geometry; sólo necesita
// existir como punto de captura del valor temporal.
std::optional<float>
readLiveSampleAt(const NodeGraph& g,
                 const ISimSession& bridge,
                 int sourceNodeId,
                 int sourcePort) {
    for (const auto& e : g.edges()) {
        if (e.fromNodeId  != sourceNodeId)               continue;
        if (attrOutputPort(e.fromAttrId) != sourcePort)  continue;
        // El consumidor de esta arista — ¿es Sink?  Si lo es, su
        // buffer contiene los samples publicados por el solver.
        const NodeInstance* dst = g.findNode(e.toNodeId);
        if (!dst) continue;
        const NodeDef& def = nodeRegistry().at(dst->type);
        if (def.category != NodeCategory::Sink) continue;
        // toAttrId puede ser input port o param; los buffers se
        // indexan por input port (canal).  Si el destino es un
        // param-pin, channel 0 es la convención.
        const int channel = attrIsInput(e.toAttrId)
                              ? attrInputPort(e.toAttrId)
                              : 0;
        auto buf = bridge.buffer(dst->id, channel);
        if (buf.empty()) continue;
        return buf.back();
    }
    return std::nullopt;
}

// evalVec3At — mini-intérprete del sub-lenguaje Vec3 (etapa 4 del
// upgrade gramatical).  Dado un output (sourceNodeId, sourcePort)
// declarado como vec(3), evalúa su valor en el frame actual:
//
//   Vec3Constant  → lee sus 3 params (x, y, z).
//   CombineXYZ    → para cada uno de sus 3 inputs scalares, busca
//                   sample vivo vía readLiveSampleAt (Sink tap).
//                   Si un input no resuelve, su componente = 0.
//   SeparateXYZ y otros → 0 vector (no soportados como source vec3
//                   en esta etapa; el plan tiene VectorAdd etc. en
//                   etapa 5).
//
// `visited` evita ciclos en evaluación recursiva.  Devuelve nullopt
// sólo si el source no existe o el grafo está malformado; cualquier
// valor faltante por bridge se silencia a 0.
// Forward decl — evalVec3At se llama a sí misma vía las ops de etapa 5.
std::optional<std::array<float, 3>>
evalVec3At(const NodeGraph& g,
           const ISimSession* bridge,
           int sourceNodeId,
           int sourcePort,
           std::unordered_set<int>& visited);

// evalScalarAt — análogo a evalVec3At para fuentes escalares.  En
// esta etapa una sola implementación: si el source es un Sink tap
// downstream alcanzable, devuelve su back().  Otros sources scalar
// (constants futuros, ops scalar) caen a nullopt → 0.
std::optional<float>
evalScalarAt(const NodeGraph& g,
             const ISimSession* bridge,
             int sourceNodeId,
             int sourcePort) {
    if (!bridge) return std::nullopt;
    return readLiveSampleAt(g, *bridge, sourceNodeId, sourcePort);
}

// Helper: evalúa el vec3 que entra al puerto `portIdx` de `nodeId`.
// Devuelve (0,0,0) si el puerto no está conectado.
std::array<float, 3>
evalVec3InputOr0(const NodeGraph& g,
                 const ISimSession* bridge,
                 int nodeId,
                 int portIdx,
                 std::unordered_set<int>& visited) {
    const Edge* e = incomingTo(g, nodeId, portIdx);
    if (!e) return { 0.f, 0.f, 0.f };
    auto v = evalVec3At(g, bridge, e->fromNodeId,
                        attrOutputPort(e->fromAttrId), visited);
    return v ? *v : std::array<float, 3>{ 0.f, 0.f, 0.f };
}

// Helper: evalúa el escalar que entra al puerto `portIdx` de `nodeId`.
float
evalScalarInputOr0(const NodeGraph& g,
                   const ISimSession* bridge,
                   int nodeId,
                   int portIdx) {
    const Edge* e = incomingTo(g, nodeId, portIdx);
    if (!e) return 0.f;
    auto v = evalScalarAt(g, bridge, e->fromNodeId,
                          attrOutputPort(e->fromAttrId));
    return v.value_or(0.f);
}

std::optional<std::array<float, 3>>
evalVec3At(const NodeGraph& g,
           const ISimSession* bridge,
           int sourceNodeId,
           int sourcePort,
           std::unordered_set<int>& visited) {
    if (!visited.insert(sourceNodeId).second)
        return std::array<float, 3>{ 0.f, 0.f, 0.f };
    const NodeInstance* src = g.findNode(sourceNodeId);
    if (!src) return std::nullopt;

    if (src->type == NodeType::Vec3Constant) {
        auto get = [&](const char* k) -> float {
            auto it = src->params.find(k);
            return it == src->params.end() ? 0.f
                                           : static_cast<float>(it->second);
        };
        return std::array<float, 3>{ get("x"), get("y"), get("z") };
    }

    if (src->type == NodeType::CombineXYZ) {
        std::array<float, 3> out { 0.f, 0.f, 0.f };
        if (bridge) {
            for (int comp = 0; comp < 3; ++comp) {
                const Edge* e = incomingTo(g, sourceNodeId, comp);
                if (!e) continue;
                auto v = readLiveSampleAt(g, *bridge,
                                          e->fromNodeId,
                                          attrOutputPort(e->fromAttrId));
                if (v) out[comp] = *v;
            }
        }
        return out;
    }

    // ---- Vector Math (etapa 5) — recursión sobre operandos -------------
    if (src->type == NodeType::VectorAdd) {
        auto a = evalVec3InputOr0(g, bridge, sourceNodeId, 0, visited);
        auto b = evalVec3InputOr0(g, bridge, sourceNodeId, 1, visited);
        return std::array<float, 3>{ a[0]+b[0], a[1]+b[1], a[2]+b[2] };
    }
    if (src->type == NodeType::VectorSub) {
        auto a = evalVec3InputOr0(g, bridge, sourceNodeId, 0, visited);
        auto b = evalVec3InputOr0(g, bridge, sourceNodeId, 1, visited);
        return std::array<float, 3>{ a[0]-b[0], a[1]-b[1], a[2]-b[2] };
    }
    if (src->type == NodeType::VectorScale) {
        auto v = evalVec3InputOr0(g, bridge, sourceNodeId, 0, visited);
        float k = evalScalarInputOr0(g, bridge, sourceNodeId, 1);
        return std::array<float, 3>{ v[0]*k, v[1]*k, v[2]*k };
    }
    if (src->type == NodeType::VectorCross) {
        auto a = evalVec3InputOr0(g, bridge, sourceNodeId, 0, visited);
        auto b = evalVec3InputOr0(g, bridge, sourceNodeId, 1, visited);
        return std::array<float, 3>{
            a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0]
        };
    }
    if (src->type == NodeType::VectorNormalize) {
        auto v = evalVec3InputOr0(g, bridge, sourceNodeId, 0, visited);
        float len2 = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
        if (len2 <= 1e-20f) return std::array<float, 3>{ 0.f, 0.f, 0.f };
        float inv = 1.f / std::sqrt(len2);
        return std::array<float, 3>{ v[0]*inv, v[1]*inv, v[2]*inv };
    }

    // Cualquier otro source vec3 (VectorDot/Length producen scalar; no
    // entran aquí — su output va por evalScalarAt) — placeholder zero.
    return std::array<float, 3>{ 0.f, 0.f, 0.f };
}

// Recursivamente baja desde un nodo del sub-lenguaje Geometry hasta
// encontrar un Object3D.  Acumula transforms al pasar por
// TransformObject (composición simple: suma de rot/trans, producto
// componente a componente de scale).  Para esta etapa los valores
// vienen de los DEFAULTS del struct — el wire a señales de bridge es
// trabajo de paso 5b.
//
// `visited` corta cualquier ciclo accidental que escape la validación
// R3.  `sinkId` viaja inalterado para etiquetar el SceneRenderable
// emitido cuando lleguemos al Object3D leaf.
void traceGeometry(const NodeGraph& graph,
                   const ISceneAssetResolver& resolver,
                   const ISimSession* bridge,
                   int currentNodeId,
                   int sinkId,
                   std::array<float, 3> accRot,
                   std::array<float, 3> accTrans,
                   std::array<float, 3> accScale,
                   std::unordered_set<int>& visited,
                   std::vector<SceneRenderable>& out) {
    if (!visited.insert(currentNodeId).second) return;   // ciclo: stop
    const NodeInstance* node = graph.findNode(currentNodeId);
    if (!node) return;

    if (node->type == NodeType::Object3D) {
        SceneRenderable r;
        r.rotation         = accRot;
        r.translation      = accTrans;
        r.scale            = accScale;
        r.sourceObject3DId = currentNodeId;
        r.sinkId           = sinkId;

        // Lee la referencia al catálogo desde stringParams.  Object3D
        // sin objectRef → renderable sin asset (placeholder, lo decide
        // el renderer).
        auto it = node->stringParams.find(kObjectRefKey);
        if (it != node->stringParams.end()) {
            r.objectRef = it->second;
            std::string objName;
            splitObjectRef(r.objectRef, objName, r.partName);
            if (!objName.empty()) r.asset = resolver.resolveByName(objName);
        }
        out.push_back(std::move(r));
        return;
    }

    if (node->type == NodeType::TransformObject) {
        // Defaults — sin source en un puerto vec(3), queda en
        // identidad por componente.
        std::array<float, 3> tRot   { 0.f, 0.f, 0.f };
        std::array<float, 3> tTrans { 0.f, 0.f, 0.f };
        std::array<float, 3> tScale { 1.f, 1.f, 1.f };

        // Helper: si hay un edge entrante al puerto `portIdx`, evalúa
        // recursivamente el vec3 del source vía evalVec3At.  Sin edge,
        // queda en el default.
        auto evalVec3Port = [&](int portIdx,
                                std::array<float, 3>& outVec)
        {
            const Edge* e = incomingTo(graph, currentNodeId, portIdx);
            if (!e) return;
            std::unordered_set<int> vecVisited;
            auto v = evalVec3At(graph, bridge,
                                e->fromNodeId,
                                attrOutputPort(e->fromAttrId),
                                vecVisited);
            if (v) outVec = *v;
        };

        evalVec3Port(1, tRot);     // rotación (rad, Euler XYZ)
        evalVec3Port(2, tTrans);   // traslación (m)
        evalVec3Port(3, tScale);   // escala (dimensionless)

        // Composición acumulada — suma componentes para rot/trans,
        // producto componente a componente para scale.  Convención
        // ZYX intrínseca para Euler (= XYZ extrínseca); el renderer
        // aplica las tres rotaciones en ese orden.
        std::array<float, 3> newRot   { accRot[0]   + tRot[0],
                                        accRot[1]   + tRot[1],
                                        accRot[2]   + tRot[2] };
        std::array<float, 3> newTrans { accTrans[0] + tTrans[0],
                                        accTrans[1] + tTrans[1],
                                        accTrans[2] + tTrans[2] };
        std::array<float, 3> newScale { accScale[0] * tScale[0],
                                        accScale[1] * tScale[1],
                                        accScale[2] * tScale[2] };

        // Baja por el input Geometry (port 0); ignora los vec3 ports.
        if (const Edge* e = incomingTo(graph, currentNodeId, /*toPort=*/0)) {
            traceGeometry(graph, resolver, bridge,
                          e->fromNodeId, sinkId,
                          newRot, newTrans, newScale,
                          visited, out);
        }
        return;
    }

    // Cualquier otro tipo en la cadena Geometry sería violación de R6
    // — terminó pasando un edge inválido.  Ignoramos sin emitir nada.
}

}  // namespace

std::vector<SceneRenderable>
collectScene(const NodeGraph& graph,
             const ISceneAssetResolver& resolver,
             const ISimSession* bridge) {
    std::vector<SceneRenderable> out;

    for (const NodeInstance& sink : graph.nodes()) {
        if (sink.type != NodeType::SceneOutput) continue;

        // SceneOutput acepta múltiples geometrías; tomamos TODAS las
        // aristas entrantes (no sólo la del primer puerto) — el R5 del
        // SceneOutput se relaja por su naturaleza multi-input.
        for (const Edge& e : graph.edges()) {
            if (e.toNodeId != sink.id) continue;
            // Cada entrada empieza con identidad — la transform vive
            // dentro del path desde Object3D hasta este sink.
            std::unordered_set<int> visited;
            traceGeometry(graph, resolver, bridge,
                          e.fromNodeId,
                          sink.id,
                          {{ 0.f, 0.f, 0.f }},
                          {{ 0.f, 0.f, 0.f }},
                          {{ 1.f, 1.f, 1.f }},
                          visited, out);
        }
    }

    return out;
}

}  // namespace scinodes
