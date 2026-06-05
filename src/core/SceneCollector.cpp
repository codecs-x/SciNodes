#include "SceneCollector.hpp"
#include "DimensionalAnalyzer.hpp"
#include "NodeInstance.hpp"
#include "NodeKind.hpp"

#include <cmath>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

// Frame compartido — mismo shape que la pila del Walker.  Lo sacamos
// del scope de Walker para que el buscador de Sinks (`findLiveSample`,
// que es agnóstico de la traversal de Geometry) pueda usarlo también.
struct SignalFrame {
    const NodeGraph* parent;
    int              containerId;
};

// findLiveSample — lee el valor actual en el cable que sale del puerto
// (sourceNodeId, sourcePort).  Versión etapa 6J.8: con el codegen
// bufferando TODOS los nodos escalares, el walker NO necesita buscar
// Sinks downstream — pregunta directo al bridge.  Si un converter
// (Rad→Deg) está entre el source y un Sink, el plot lee grados y el
// walker 3D lee radianes en sus respectivos puntos del cable, cada uno
// independiente del otro.
//
// Único caso que requiere recursión: cuando el source es un stub
// (SubGraphInput o SubGraph container), su id no existe en el namespace
// flat del bridge — hay que resolver al nodo real que provee el valor.
std::optional<float>
findLiveSample(const NodeGraph& g,
               const ISimSession& bridge,
               int sourceNodeId, int sourcePort,
               std::vector<SignalFrame>& stack)
{
    const NodeInstance* src = g.findNode(sourceNodeId);
    if (!src) return std::nullopt;

    // (a) Source es un SubGraphInput stub: el bridge no lo conoce
    // (codegen lo aplana).  Saltamos al padre y resolvemos al source
    // externo que alimenta el container.in[stub.Port].
    if (src->type == NodeType::SubGraphInput) {
        if (stack.empty()) return std::nullopt;
        const auto frame = stack.back();
        int containerPort = 0;
        if (auto pIt = src->params.find("Port"); pIt != src->params.end())
            containerPort = static_cast<int>(pIt->second);
        for (const Edge& e : frame.parent->edges()) {
            if (e.toNodeId != frame.containerId) continue;
            if (attrInputPort(e.toAttrId) != containerPort) continue;
            stack.pop_back();
            auto v = findLiveSample(*frame.parent, bridge,
                                    e.fromNodeId,
                                    attrOutputPort(e.fromAttrId), stack);
            stack.push_back(frame);
            return v;
        }
        return std::nullopt;
    }

    // (b) Source es un SubGraph container: bajamos al hijo, encontramos
    // el SubGraphOutput stub con Port == sourcePort, y resolvemos al
    // source interno que lo alimenta.
    if (src->type == NodeType::SubGraph) {
        const NodeGraph* child = g.subGraphOf(sourceNodeId);
        if (!child) return std::nullopt;
        const NodeInstance* stub = nullptr;
        for (const NodeInstance& c : child->nodes()) {
            if (c.type != NodeType::SubGraphOutput) continue;
            int port = 0;
            if (auto pIt = c.params.find("Port"); pIt != c.params.end())
                port = static_cast<int>(pIt->second);
            if (port == sourcePort) { stub = &c; break; }
        }
        if (!stub) return std::nullopt;
        for (const Edge& e : child->edges()) {
            if (e.toNodeId != stub->id) continue;
            if (attrInputPort(e.toAttrId) != 0) continue;
            stack.push_back({ &g, sourceNodeId });
            auto v = findLiveSample(*child, bridge,
                                    e.fromNodeId,
                                    attrOutputPort(e.fromAttrId), stack);
            stack.pop_back();
            return v;
        }
        return std::nullopt;
    }

    // (c) Source común: el bridge tiene un buffer para él (etapa 6J.8).
    auto buf = bridge.buffer(sourceNodeId, sourcePort);
    if (buf.empty()) return std::nullopt;
    return buf.back();
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
           std::unordered_set<int>& visited,
           std::vector<SignalFrame>& stack);

// evalScalarAt — análogo a evalVec3At para fuentes escalares.  En
// esta etapa una sola implementación: si el source es un Sink tap
// downstream alcanzable, devuelve su back().  Otros sources scalar
// (constants futuros, ops scalar) caen a nullopt → 0.
std::optional<float>
evalScalarAt(const NodeGraph& g,
             const ISimSession* bridge,
             int sourceNodeId,
             int sourcePort,
             std::vector<SignalFrame>& stack) {
    if (!bridge) return std::nullopt;
    return findLiveSample(g, *bridge, sourceNodeId, sourcePort, stack);
}

// Helper: evalúa el vec3 que entra al puerto `portIdx` de `nodeId`.
// Devuelve (0,0,0) si el puerto no está conectado.
std::array<float, 3>
evalVec3InputOr0(const NodeGraph& g,
                 const ISimSession* bridge,
                 int nodeId,
                 int portIdx,
                 std::unordered_set<int>& visited,
                 std::vector<SignalFrame>& stack) {
    const Edge* e = incomingTo(g, nodeId, portIdx);
    if (!e) return { 0.f, 0.f, 0.f };
    auto v = evalVec3At(g, bridge, e->fromNodeId,
                        attrOutputPort(e->fromAttrId), visited, stack);
    return v ? *v : std::array<float, 3>{ 0.f, 0.f, 0.f };
}

// Helper: evalúa el escalar que entra al puerto `portIdx` de `nodeId`.
float
evalScalarInputOr0(const NodeGraph& g,
                   const ISimSession* bridge,
                   int nodeId,
                   int portIdx,
                   std::vector<SignalFrame>& stack) {
    const Edge* e = incomingTo(g, nodeId, portIdx);
    if (!e) return 0.f;
    auto v = evalScalarAt(g, bridge, e->fromNodeId,
                          attrOutputPort(e->fromAttrId), stack);
    return v.value_or(0.f);
}

// ---------------------------------------------------------------------------
// Sub-lenguaje Vec3 — evaluadores por producción (etapa 6J.2).
//
// Cada función es una producción gramatical pura: dado el NodeInstance
// fuente y el contexto del walker (grafo, bridge, stack, visited),
// devuelve el vec3 que ESA producción emite.  Agregar una producción
// nueva = nueva función + entrada en `lookupVec3Eval`.  El walker
// (evalVec3At) no necesita saber sobre el nuevo NodeType — la tabla
// despacha por él.  Mismo patrón que `lookupTraceHook` del walker de
// Geometry.
//
// Signature uniforme: `(NodeInstance, NodeGraph, bridge, visited, stack)
// → array<float,3>`.  El default cuando no hay hook registrado es
// (0, 0, 0) — manejado por evalVec3At, no por cada hook.
// ---------------------------------------------------------------------------
using Vec3EvalFn = std::array<float, 3>(*)(
    const NodeInstance&,
    const NodeGraph&,
    const ISimSession*,
    std::unordered_set<int>&,
    std::vector<SignalFrame>&);

std::array<float, 3>
evalVec3Constant(const NodeInstance& src, const NodeGraph& /*g*/,
                 const ISimSession* /*bridge*/,
                 std::unordered_set<int>& /*visited*/,
                 std::vector<SignalFrame>& /*stack*/)
{
    auto get = [&](const char* k) -> float {
        auto it = src.params.find(k);
        return it == src.params.end() ? 0.f
                                      : static_cast<float>(it->second);
    };
    return { get("x"), get("y"), get("z") };
}

std::array<float, 3>
evalCombineXYZ(const NodeInstance& src, const NodeGraph& g,
               const ISimSession* bridge,
               std::unordered_set<int>& /*visited*/,
               std::vector<SignalFrame>& stack)
{
    std::array<float, 3> out { 0.f, 0.f, 0.f };
    if (!bridge) return out;
    for (int comp = 0; comp < 3; ++comp) {
        const Edge* e = incomingTo(g, src.id, comp);
        if (!e) continue;
        if (auto v = findLiveSample(g, *bridge,
                                    e->fromNodeId,
                                    attrOutputPort(e->fromAttrId),
                                    stack))
            out[comp] = *v;
    }
    return out;
}

std::array<float, 3>
evalVectorAdd(const NodeInstance& src, const NodeGraph& g,
              const ISimSession* bridge,
              std::unordered_set<int>& visited,
              std::vector<SignalFrame>& stack)
{
    auto a = evalVec3InputOr0(g, bridge, src.id, 0, visited, stack);
    auto b = evalVec3InputOr0(g, bridge, src.id, 1, visited, stack);
    return { a[0]+b[0], a[1]+b[1], a[2]+b[2] };
}

std::array<float, 3>
evalVectorSub(const NodeInstance& src, const NodeGraph& g,
              const ISimSession* bridge,
              std::unordered_set<int>& visited,
              std::vector<SignalFrame>& stack)
{
    auto a = evalVec3InputOr0(g, bridge, src.id, 0, visited, stack);
    auto b = evalVec3InputOr0(g, bridge, src.id, 1, visited, stack);
    return { a[0]-b[0], a[1]-b[1], a[2]-b[2] };
}

std::array<float, 3>
evalVectorScale(const NodeInstance& src, const NodeGraph& g,
                const ISimSession* bridge,
                std::unordered_set<int>& visited,
                std::vector<SignalFrame>& stack)
{
    auto v = evalVec3InputOr0(g, bridge, src.id, 0, visited, stack);
    float k = evalScalarInputOr0(g, bridge, src.id, 1, stack);
    return { v[0]*k, v[1]*k, v[2]*k };
}

std::array<float, 3>
evalVectorCross(const NodeInstance& src, const NodeGraph& g,
                const ISimSession* bridge,
                std::unordered_set<int>& visited,
                std::vector<SignalFrame>& stack)
{
    auto a = evalVec3InputOr0(g, bridge, src.id, 0, visited, stack);
    auto b = evalVec3InputOr0(g, bridge, src.id, 1, visited, stack);
    return { a[1]*b[2] - a[2]*b[1],
             a[2]*b[0] - a[0]*b[2],
             a[0]*b[1] - a[1]*b[0] };
}

std::array<float, 3>
evalVectorNormalize(const NodeInstance& src, const NodeGraph& g,
                    const ISimSession* bridge,
                    std::unordered_set<int>& visited,
                    std::vector<SignalFrame>& stack)
{
    auto v = evalVec3InputOr0(g, bridge, src.id, 0, visited, stack);
    float len2 = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    // El umbral 1e-20 es zero-norm guard: vec3 con magnitud menor que la
    // raíz se considera cero (la dirección no está definida).
    if (len2 <= 1e-20f) return { 0.f, 0.f, 0.f };
    float inv = 1.f / std::sqrt(len2);
    return { v[0]*inv, v[1]*inv, v[2]*inv };
}

// Tabla funcional NodeType → evaluador.  Los nodos cuya salida es
// scalar (VectorDot, VectorLength) NO aparecen aquí — los consume
// evalScalarAt vía findLiveSample.
Vec3EvalFn lookupVec3Eval(NodeType t) {
    static const std::unordered_map<NodeType, Vec3EvalFn> kEvals = {
        { NodeType::Vec3Constant,    &evalVec3Constant    },
        { NodeType::CombineXYZ,      &evalCombineXYZ      },
        { NodeType::VectorAdd,       &evalVectorAdd       },
        { NodeType::VectorSub,       &evalVectorSub       },
        { NodeType::VectorScale,     &evalVectorScale     },
        { NodeType::VectorCross,     &evalVectorCross     },
        { NodeType::VectorNormalize, &evalVectorNormalize },
    };
    if (auto it = kEvals.find(t); it != kEvals.end()) return it->second;
    return nullptr;
}

std::optional<std::array<float, 3>>
evalVec3At(const NodeGraph& g,
           const ISimSession* bridge,
           int sourceNodeId,
           int /*sourcePort*/,
           std::unordered_set<int>& visited,
           std::vector<SignalFrame>& stack)
{
    if (!visited.insert(sourceNodeId).second)
        return std::array<float, 3>{ 0.f, 0.f, 0.f };
    const NodeInstance* src = g.findNode(sourceNodeId);
    if (!src) return std::nullopt;
    if (Vec3EvalFn fn = lookupVec3Eval(src->type))
        return fn(*src, g, bridge, visited, stack);
    // Sin evaluador registrado: silencio (vec3 cero).  Cualquier
    // NodeType nuevo que devuelva vec3 entra como entrada en la tabla,
    // no como `else if` acá.
    return std::array<float, 3>{ 0.f, 0.f, 0.f };
}

// ---------------------------------------------------------------------------
// Geometry walker (etapa 6J.1) — dispatch polimórfico por kind.
//
// Distinción de capas (preferencia del usuario: gramática + SOLID):
//
//   - ESTRUCTURAL (cómo crece el grafo): `std::visit` sobre el sum-type
//     `NodeKind`.  Las productions BuiltinKind / SubGraphContainerKind /
//     SubGraphInputKind / SubGraphOutputKind / CustomKind tienen cada
//     una su propio overload de `traceAsSource`.  El compilador exige
//     exhaustividad — agregar una kind nueva sin su overload no compila.
//
//   - COMPORTAMENTAL (qué hace un nodo concreto en la traversal):
//     hooks funcionales indexados por NodeType.  La función
//     `lookupTraceHook` es la ÚNICA tabla de dispatch sobre NodeType en
//     el walker — agregar Object3D / TransformObject / etc. con nueva
//     semántica es agregar una entrada al map, no un `if` nuevo.
//
// Sin esta separación, una conexión Geometry que cruza la frontera de
// un SubGraph se rompía en silencio: el walker viejo veía el contenedor
// y se quedaba sin saber qué hacer (`Cualquier otro tipo... ignoramos`).
// ---------------------------------------------------------------------------
struct Walker {
    const ISceneAssetResolver& resolver;
    const ISimSession*         bridge;
    int                        sinkId;
    std::array<float, 3>       accRot   { 0.f, 0.f, 0.f };
    std::array<float, 3>       accTrans { 0.f, 0.f, 0.f };
    std::array<float, 3>       accScale { 1.f, 1.f, 1.f };
    std::array<float, 3>       accPivot { 0.f, 0.f, 0.f };

    // Pila de frames para SubGraph descent — compartida con el
    // tap-finder de Sinks (findLiveSample) para que crucen las
    // mismas fronteras al unísono.  Cuando bajamos al hijo de un
    // container empujamos el (padre, containerId); cuando un stub
    // SubGraphInput "asciende" lee el top del stack para localizar el
    // edge externo correspondiente.
    std::vector<SignalFrame> stack;

    // Visitados keyed por (grafo, nodeId).  IDs se preservan al
    // encapsular (etapa 6I.U.b), así que el mismo id puede aparecer en
    // padre y hijo: distinguir por puntero al grafo evita falsos ciclos.
    std::set<std::pair<const NodeGraph*, int>> visited;

    std::vector<SceneRenderable>& out;
};

// Forward decl del driver — los hooks por NodeType y los overloads por
// kind lo llaman para continuar la traversal.
void walkFrom(Walker& w, const NodeInstance& n, int outPort,
              const NodeGraph& g);

// ---- Comportamiento por NodeType (capa funcional) ------------------------
//
// Cada hook es una función pura sobre el estado del walker.  Sin
// argumentos implícitos ni knowledge del switch — el dispatch lo hace
// `lookupTraceHook` por tabla, y cada hook decide qué hacer con el
// instance que le toca.

using TraceHook = void(*)(const NodeInstance&, int outPort,
                          Walker&, const NodeGraph&);

void traceObject3D(const NodeInstance& n, int /*outPort*/,
                   Walker& w, const NodeGraph& /*g*/) {
    SceneRenderable r;
    r.rotation         = w.accRot;
    r.translation      = w.accTrans;
    r.scale            = w.accScale;
    r.pivot            = w.accPivot;
    r.sourceObject3DId = n.id;
    r.sinkId           = w.sinkId;
    if (auto it = n.stringParams.find(kObjectRefKey);
        it != n.stringParams.end())
    {
        r.objectRef = it->second;
        std::string objName;
        splitObjectRef(r.objectRef, objName, r.partName);
        if (!objName.empty()) r.asset = w.resolver.resolveByName(objName);
    }
    w.out.push_back(std::move(r));
}

void traceTransformObject(const NodeInstance& n, int /*outPort*/,
                          Walker& w, const NodeGraph& g) {
    std::array<float, 3> tRot   { 0.f, 0.f, 0.f };
    std::array<float, 3> tTrans { 0.f, 0.f, 0.f };
    std::array<float, 3> tScale { 1.f, 1.f, 1.f };
    std::array<float, 3> tPivot { 0.f, 0.f, 0.f };

    auto evalVec3Port = [&](int portIdx, std::array<float, 3>& outVec) {
        const Edge* e = incomingTo(g, n.id, portIdx);
        if (!e) return;
        std::unordered_set<int> vecVisited;
        if (auto v = evalVec3At(g, w.bridge,
                                e->fromNodeId,
                                attrOutputPort(e->fromAttrId),
                                vecVisited, w.stack))
            outVec = *v;
    };
    evalVec3Port(1, tRot);     // rotación (rad, Euler XYZ)
    evalVec3Port(2, tTrans);   // traslación (m)
    evalVec3Port(3, tScale);   // escala (dimensionless)
    evalVec3Port(4, tPivot);   // pivote (m, centro de rotación)

    // Save/restore en lugar de copia local — el accumulator vive en el
    // walker.  Composición: suma para rot/trans, producto para scale.
    // El pivote NO se acumula: cada TransformObject define su propio
    // centro de rotación; en una cadena gana el más cercano a la
    // geometría (el último en escribirse antes de llegar al Object3D).
    const auto savedRot   = w.accRot;
    const auto savedTrans = w.accTrans;
    const auto savedScale = w.accScale;
    const auto savedPivot = w.accPivot;
    w.accRot   = { savedRot[0]   + tRot[0],   savedRot[1]   + tRot[1],   savedRot[2]   + tRot[2]   };
    w.accTrans = { savedTrans[0] + tTrans[0], savedTrans[1] + tTrans[1], savedTrans[2] + tTrans[2] };
    w.accScale = { savedScale[0] * tScale[0], savedScale[1] * tScale[1], savedScale[2] * tScale[2] };
    w.accPivot = tPivot;

    if (const Edge* e = incomingTo(g, n.id, /*toPort=*/0)) {
        if (const NodeInstance* up = g.findNode(e->fromNodeId))
            walkFrom(w, *up, attrOutputPort(e->fromAttrId), g);
    }

    w.accRot   = savedRot;
    w.accTrans = savedTrans;
    w.accScale = savedScale;
    w.accPivot = savedPivot;
}

// Tabla functional NodeType→hook.  Resolver(NodeType) → fn o nullptr.
// Agregar Geometry semantics para un type nuevo = una línea en este map.
TraceHook lookupTraceHook(NodeType t) {
    static const std::unordered_map<NodeType, TraceHook> kHooks = {
        { NodeType::Object3D,        &traceObject3D        },
        { NodeType::TransformObject, &traceTransformObject },
    };
    if (auto it = kHooks.find(t); it != kHooks.end()) return it->second;
    return nullptr;
}

// ---- Dispatch estructural por NodeKind (capa OOP/grammar) ---------------
//
// Cada overload es una producción gramatical en el sub-lenguaje Geometry.
// `std::visit` selecciona la correcta sin que el caller mire `n.type`.

void traceAsSource(const scinodes::BuiltinKind&,
                   const NodeInstance& n, int outPort,
                   Walker& w, const NodeGraph& g)
{
    // Builtin delega a la tabla de hooks.  Nodos sin hook (la mayoría,
    // que no participan en Geometry) son no-op silencioso.
    if (TraceHook h = lookupTraceHook(n.type)) h(n, outPort, w, g);
}

void traceAsSource(const scinodes::CustomKind&,
                   const NodeInstance&, int, Walker&, const NodeGraph&)
{
    // Los Custom nodes no participan en Geometry hoy.  Si en el futuro
    // un Custom emite renderables, este overload llamará al hook que el
    // descriptor JSON declare.
}

void traceAsSource(const scinodes::SubGraphContainerKind&,
                   const NodeInstance& n, int outPort,
                   Walker& w, const NodeGraph& g)
{
    // Descender al hijo y arrancar desde el SubGraphOutput stub cuyo
    // `Port` coincide con el output que estamos consumiendo.
    const NodeGraph* child = g.subGraphOf(n.id);
    if (!child) return;
    const NodeInstance* stub = nullptr;
    for (const NodeInstance& c : child->nodes()) {
        if (c.type != NodeType::SubGraphOutput) continue;
        int port = 0;
        if (auto pIt = c.params.find("Port"); pIt != c.params.end())
            port = static_cast<int>(pIt->second);
        if (port == outPort) { stub = &c; break; }
    }
    if (!stub) return;
    const Edge* e = incomingTo(*child, stub->id, /*toPort=*/0);
    if (!e) return;
    const NodeInstance* up = child->findNode(e->fromNodeId);
    if (!up) return;
    // Empujamos frame: si la cadena adentro llega a un SubGraphInput
    // stub, sabe que tiene que asomarse al padre por `n.id`.
    w.stack.push_back({ &g, n.id });
    walkFrom(w, *up, attrOutputPort(e->fromAttrId), *child);
    w.stack.pop_back();
}

void traceAsSource(const scinodes::SubGraphInputKind&,
                   const NodeInstance& n, int /*outPort*/,
                   Walker& w, const NodeGraph& /*g*/)
{
    // Pop al frame del padre y seguir por el edge que alimenta el
    // puerto exterior del container correspondiente a este stub.
    if (w.stack.empty()) return;
    const auto frame = w.stack.back();
    int containerInputPort = 0;
    if (auto pIt = n.params.find("Port"); pIt != n.params.end())
        containerInputPort = static_cast<int>(pIt->second);
    const Edge* e = incomingTo(*frame.parent,
                               frame.containerId, containerInputPort);
    if (!e) return;
    const NodeInstance* up = frame.parent->findNode(e->fromNodeId);
    if (!up) return;
    w.stack.pop_back();
    walkFrom(w, *up, attrOutputPort(e->fromAttrId), *frame.parent);
    w.stack.push_back(frame);
}

void traceAsSource(const scinodes::SubGraphOutputKind&,
                   const NodeInstance&, int, Walker&, const NodeGraph&)
{
    // El stub Output no es un source — el walker arranca desde acá sólo
    // si alguien armó manualmente un edge inválido (R6 lo prohíbe).
    // Silent no-op.
}

void walkFrom(Walker& w, const NodeInstance& n, int outPort,
              const NodeGraph& g)
{
    if (!w.visited.insert({ &g, n.id }).second) return;   // ciclo guard
    std::visit([&](const auto& k) { traceAsSource(k, n, outPort, w, g); },
               scinodes::kindOf(n));
}

}  // namespace

std::vector<SceneRenderable>
collectScene(const NodeGraph& graph,
             const ISceneAssetResolver& resolver,
             const ISimSession* bridge)
{
    std::vector<SceneRenderable> out;
    for (const NodeInstance& sink : graph.nodes()) {
        if (sink.type != NodeType::SceneOutput) continue;
        for (const Edge& e : graph.edges()) {
            if (e.toNodeId != sink.id) continue;
            const NodeInstance* up = graph.findNode(e.fromNodeId);
            if (!up) continue;
            Walker w{ resolver, bridge, sink.id,
                      {{ 0.f, 0.f, 0.f }}, {{ 0.f, 0.f, 0.f }},
                      {{ 1.f, 1.f, 1.f }}, {{ 0.f, 0.f, 0.f }},
                      {}, {}, out };
            walkFrom(w, *up, attrOutputPort(e.fromAttrId), graph);
        }
    }
    return out;
}

}  // namespace scinodes
