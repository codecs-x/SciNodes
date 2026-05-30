#include "NodeKind.hpp"
#include "NodeInstance.hpp"
#include "CustomNodeRegistry.hpp"
#include "Field.hpp"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace scinodes {

// NodeInstance vive en namespace global — traemos el nombre adentro
// para mantener el resto del cpp libre de `::NodeInstance` repetido.
using ::NodeInstance;

// Verificación estática: toda alternativa de la variant satisface el
// contrato.  Si alguien agrega un kind nuevo a NodeKind sin implementar
// resolveDef, el compilador lo detecta acá — no en un call-site lejano.
static_assert(NodeKindLike<BuiltinKind>);
static_assert(NodeKindLike<CustomKind>);
static_assert(NodeKindLike<SubGraphContainerKind>);
static_assert(NodeKindLike<SubGraphInputKind>);
static_assert(NodeKindLike<SubGraphOutputKind>);

// ---------------------------------------------------------------------------
// Helpers internos — cachés de NodeDef sintetizados.  Antes vivían en
// NodeInstance.cpp con visibilidad anónima; al mover el dispatch acá
// también movemos sus caches.
// ---------------------------------------------------------------------------
namespace {

// Stub devuelto por CustomKind cuando el typeId no resuelve — keeps
// grammar/codegen from crashing on a typo while still being clearly
// identifiable.
const NodeDef& unknownCustomDef() {
    static const NodeDef stub = {
        NodeType::Custom, NodeCategory::Transformer,
        "<unknown custom>", "Custom node referencing a missing descriptor.",
        1, 1, {}
    };
    return stub;
}

const NodeDef& synthesizeCustomDef(const std::string& typeId) {
    static std::mutex                                cacheMtx;
    static std::unordered_map<std::string, NodeDef>  cache;

    std::lock_guard<std::mutex> lock(cacheMtx);
    auto it = cache.find(typeId);
    if (it != cache.end()) return it->second;

    const auto* cd = scinodes::customNodes().find(typeId);
    if (!cd) return unknownCustomDef();

    NodeDef def{
        NodeType::Custom, cd->category,
        cd->label, cd->description,
        cd->inputPorts, cd->outputPorts,
        cd->params
    };
    auto [ins, _] = cache.emplace(typeId, std::move(def));
    return ins->second;
}

// Firma estable de un vector de TypeExpr — reusa describeType (lenguaje
// canónico de tipos) para que la cache key crezca con la gramática.
std::string typeExprVecSig(const std::vector<TypeExpr>& v, int paddedSize) {
    std::string s;
    for (int i = 0; i < paddedSize; ++i) {
        s += (i < static_cast<int>(v.size())) ? describeType(v[i])
                                              : "scalar";
        s += ';';
    }
    return s;
}

// Extrae el TypeExpr override de un puerto particular.  Si la entrada
// no existe, devuelve scalar (el default histórico).  Usado para
// construir las firmas que se pasan al cache de SubGraph defs.
TypeExpr overrideOr(const NodeInstance& n, int key) {
    auto it = n.portTypeOverrides.find(key);
    return (it != n.portTypeOverrides.end()) ? it->second : exprScalar();
}

// Vector ordenado de overrides para los `count` inputs/outputs del
// container — el contenedor expone N entradas (claves 0..N-1) y M
// salidas (claves 9000..9000+M-1).
std::vector<TypeExpr> orderedInputTypes(const NodeInstance& n, int count) {
    std::vector<TypeExpr> v;
    v.reserve(count);
    for (int i = 0; i < count; ++i)
        v.push_back(overrideOr(n, portKeyForInput(i)));
    return v;
}
std::vector<TypeExpr> orderedOutputTypes(const NodeInstance& n, int count) {
    std::vector<TypeExpr> v;
    v.reserve(count);
    for (int i = 0; i < count; ++i)
        v.push_back(overrideOr(n, portKeyForOutput(i)));
    return v;
}

// Categoría derivada del arity del SubGraph — sin esto, el BFS de la
// gramática no arranca desde SubGraphs source-like:
//   inCount  == 0  → Source       (genera señal interna, sin entradas)
//   outCount == 0  → Sink         (consume y no devuelve nada)
//   otherwise      → Transformer  (pipeline intermedio)
NodeCategory subGraphCategoryFor(int inCount, int outCount) {
    if (inCount  == 0 && outCount > 0) return NodeCategory::Source;
    if (outCount == 0 && inCount  > 0) return NodeCategory::Sink;
    return NodeCategory::Transformer;
}

// Sintetiza y cachea un NodeDef para SubGraph container.  La key
// incluye la firma de tipos porque encapsular Geometry vs Scalar con
// el mismo arity DEBE producir defs distintos (R6 los discrimina).
const NodeDef& synthesizeSubGraphDef(
        int inCount, int outCount,
        const std::vector<TypeExpr>& sigIn,
        const std::vector<TypeExpr>& sigOut)
{
    static std::mutex                       cacheMtx;
    static std::map<std::string, NodeDef>   cache;

    std::string key = std::to_string(inCount)  + "|" +
                      std::to_string(outCount) + "|" +
                      typeExprVecSig(sigIn,  inCount) + "|" +
                      typeExprVecSig(sigOut, outCount);

    std::lock_guard<std::mutex> lock(cacheMtx);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    NodeDef def {
        NodeType::SubGraph, subGraphCategoryFor(inCount, outCount),
        "SubGraph",
        "Sub-grafo recursivo (paréntesis).  Doble-click para entrar.",
        inCount, outCount, {}
    };
    def.inputPortTypes.reserve(inCount);
    def.outputPortTypes.reserve(outCount);
    for (int i = 0; i < inCount;  ++i)
        def.inputPortTypes.push_back(
            i < static_cast<int>(sigIn.size())  ? sigIn[i]  : exprScalar());
    for (int i = 0; i < outCount; ++i)
        def.outputPortTypes.push_back(
            i < static_cast<int>(sigOut.size()) ? sigOut[i] : exprScalar());
    auto ins = cache.emplace(std::move(key), std::move(def));
    return ins.first->second;
}

// Stubs con TypeExpr override en su único puerto.  Cacheamos por
// (alternativa de variant, describeType(port)); la mayoría son escalares
// y comparten una entrada.
const NodeDef& synthesizeStubDef(NodeType kind, const TypeExpr& portType) {
    static std::mutex                     cacheMtx;
    static std::map<std::string, NodeDef> cache;

    std::string key = std::to_string(static_cast<int>(kind)) + "|" +
                      describeType(portType);
    std::lock_guard<std::mutex> lock(cacheMtx);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    NodeDef def = nodeRegistry().at(kind);   // copia el registry base
    if (kind == NodeType::SubGraphInput)  def.outputPortTypes = { portType };
    else                                  def.inputPortTypes  = { portType };
    auto ins = cache.emplace(std::move(key), std::move(def));
    return ins.first->second;
}

}  // namespace

// ---------------------------------------------------------------------------
// Implementación de resolveDef por kind.
// ---------------------------------------------------------------------------
const NodeDef& BuiltinKind::resolveDef(const ::NodeInstance& n) const {
    return nodeRegistry().at(n.type);
}

const NodeDef& CustomKind::resolveDef(const NodeInstance& n) const {
    return synthesizeCustomDef(n.customType);
}

const NodeDef& SubGraphContainerKind::resolveDef(const NodeInstance& n) const {
    return synthesizeSubGraphDef(n.subGraphInputCount,
                                 n.subGraphOutputCount,
                                 orderedInputTypes (n, n.subGraphInputCount),
                                 orderedOutputTypes(n, n.subGraphOutputCount));
}

const NodeDef& SubGraphInputKind::resolveDef(const NodeInstance& n) const {
    const TypeExpr t = overrideOr(n, portKeyForOutput(0));
    if (!isScalarType(t)) return synthesizeStubDef(NodeType::SubGraphInput, t);
    return nodeRegistry().at(NodeType::SubGraphInput);
}

const NodeDef& SubGraphOutputKind::resolveDef(const NodeInstance& n) const {
    const TypeExpr t = overrideOr(n, portKeyForInput(0));
    if (!isScalarType(t)) return synthesizeStubDef(NodeType::SubGraphOutput, t);
    return nodeRegistry().at(NodeType::SubGraphOutput);
}

// ---------------------------------------------------------------------------
// kindOf — el único discriminator-boundary.  Cualquier dispatch posterior
// pasa por std::visit sobre el resultado, no por más if/switch sobre
// NodeType.  El switch acá es la frontera natural entre el mundo enum
// (legacy, persistido en .scn) y el mundo gramatical (variant).
// ---------------------------------------------------------------------------
NodeKind kindOf(NodeType t, std::string_view /*customTypeId*/) {
    using enum NodeType;
    switch (t) {
    case Custom:          return CustomKind{};
    case SubGraph:        return SubGraphContainerKind{};
    case SubGraphInput:   return SubGraphInputKind{};
    case SubGraphOutput:  return SubGraphOutputKind{};
    default:              return BuiltinKind{};
    }
}

NodeKind kindOf(const NodeInstance& n) {
    return kindOf(n.type, n.customType);
}

// ---------------------------------------------------------------------------
// Operaciones libres: comportamiento funcional sobre la jerarquía.
// ---------------------------------------------------------------------------
const NodeDef& resolveDef(const NodeInstance& n) {
    // std::visit con genérico-auto: el compilador instancia un overload por
    // alternativa de NodeKind y exige que cada uno implemente resolveDef
    // (lo cual el concept ya garantiza).
    return std::visit(
        [&](const auto& k) -> const NodeDef& { return k.resolveDef(n); },
        kindOf(n));
}

void seedFields(NodeInstance& inst) {
    const NodeDef& def = resolveDef(inst);
    for (const auto& fd : synthesizeFields(def))
        inst.fields[fd.name] = fd.defaultQuantity;
}

std::optional<std::pair<int, int>> aliasTargetOf(const NodeInstance& n) {
    // Hoy sólo NodeType::Alias usa esta convención.  La función centraliza
    // qué cuenta como alias para que los call sites (analyzer, topoSort,
    // codegen) no repitan la dispatch sobre NodeType.
    if (n.type != NodeType::Alias) return std::nullopt;
    auto itTid = n.params.find("target_node_id");
    if (itTid == n.params.end()) return std::nullopt;
    const int tid = static_cast<int>(itTid->second);
    if (tid <= 0) return std::nullopt;
    auto itTpt = n.params.find("target_port");
    const int tpt = (itTpt != n.params.end())
                      ? static_cast<int>(itTpt->second) : 0;
    return std::make_pair(tid, tpt);
}

}  // namespace scinodes
