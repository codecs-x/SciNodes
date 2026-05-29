#pragma once

// ---------------------------------------------------------------------------
// NodeKind — gramática estructural como sum-type (etapa 6J.1).
//
// Cada alternativa de la variant ES una producción de la gramática.
// Las operaciones que el resto del código necesita (resolver el NodeDef,
// caminar el sub-lenguaje Geometry, etc.) se despachan vía `std::visit`
// con un overload-set; el compilador exige exhaustividad en la
// definición de la operación, no en los call-sites.
//
// Distinción explícita (preferencia del usuario):
//
//   - Arquitectura / SOLID: cada producción tiene su propio TIPO.  Open/
//     Closed por sustitución estructural — agregar una producción nueva
//     = agregar un struct + agregar la alternativa al `using NodeKind`
//     + implementar `resolveDef`.  Ningún switch en el resto del código.
//
//   - Comportamiento interno: funciones puras sobre los datos del nodo
//     (ver `seedFields` libre + `geometry::walkFromOutput`).
//
// El ÚNICO sitio donde `NodeType` (enum discriminator) se traduce a
// `NodeKind` (variant tag) es la función `kindOf`.  Cualquier nueva
// dispatch sobre `n.type` debe pasar a ser una operación sobre el kind.
// ---------------------------------------------------------------------------

#include "NodeType.hpp"

#include <concepts>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

// NodeInstance vive en el namespace global (decisión histórica que aún
// sobrevive — varios sitios lo usan sin ::scinodes::).  Lo forward-declaramos
// fuera de scinodes y usamos `::NodeInstance` dentro.
struct NodeInstance;

namespace scinodes {

// -----------------------------------------------------------------------
// Producciones estructurales — un struct por categoría con semántica de
// resolución de def distinta.  La distinción es ESTRUCTURAL (qué forma
// tiene el NodeDef, cómo se sintetiza), no comportamental (qué hace el
// solver/walker con él) — eso vive en módulos separados (`GeometryWalker`,
// `ScilabCodeGen`, ...) y se despacha sobre los mismos kinds.
//
// Los structs son vacíos (cero estado): el estado per-instance vive en
// el `NodeInstance`.  El kind es solamente la "rule" gramatical activa.
// -----------------------------------------------------------------------

// Producción ordinaria: la mayoría de NodeTypes — el def vive estático
// en `nodeRegistry()`.  Incluye Alias (cuya semántica de traversal vive
// en el codegen, no acá).
struct BuiltinKind {
    const NodeDef& resolveDef(const ::NodeInstance&) const;
};

// Producción Custom: el def se sintetiza desde `CustomNodeRegistry`
// usando `inst.customType` como llave.  Cacheada en NodeKind.cpp.
struct CustomKind {
    const NodeDef& resolveDef(const ::NodeInstance&) const;
};

// Producción SubGraph (contenedor recursivo).  El def se sintetiza con
// el conteo y tipos de los stubs internos — cada instancia puede tener
// una "signature" exterior distinta.  Cacheada por signature en
// NodeKind.cpp.
struct SubGraphContainerKind {
    const NodeDef& resolveDef(const ::NodeInstance&) const;
};

// Producciones stub: representan un único puerto exterior del padre,
// vistos desde adentro.  El tipo del puerto sale del override per-instance
// (cuando encapsular envolvió un cable Geometry / vec(N) / ...).
struct SubGraphInputKind {
    const NodeDef& resolveDef(const ::NodeInstance&) const;
};
struct SubGraphOutputKind {
    const NodeDef& resolveDef(const ::NodeInstance&) const;
};

// La gramática completa: sum-type cerrado.  Crece sólo agregando
// alternatives aquí, lo que fuerza al compilador a revisar todo
// std::visit posterior.
using NodeKind = std::variant<BuiltinKind,
                              CustomKind,
                              SubGraphContainerKind,
                              SubGraphInputKind,
                              SubGraphOutputKind>;

// Concept: cualquier alternativa debe poder resolver su NodeDef.  Los
// static_asserts en NodeKind.cpp aplican el concept a cada alternative.
template <typename K>
concept NodeKindLike = requires(const K& k, const ::NodeInstance& n) {
    { k.resolveDef(n) } -> std::same_as<const NodeDef&>;
};

// -----------------------------------------------------------------------
// El único discriminator→kind boundary.  Toda otra dispatch sobre
// `NodeType` debe pasar a ser std::visit sobre el resultado.
//
// `customTypeId` es opcional; lo recibe `CustomKind` cuando hace falta
// (no se usa para resolveDef — eso lee `inst.customType`).
// -----------------------------------------------------------------------
NodeKind kindOf(NodeType t, std::string_view customTypeId = {});
NodeKind kindOf(const ::NodeInstance&);

// -----------------------------------------------------------------------
// Operaciones libres sobre el kind — comportamiento funcional sobre la
// jerarquía gramatical.  Cada nueva operación es un nuevo header/cpp
// que hace su propio std::visit; no toca este archivo.
// -----------------------------------------------------------------------

// Sintetiza el NodeDef vía kindOf+visit.  Replaza la cadena de if/else
// que vivía en `defOf(NodeInstance)` (etapa pre-6J).
const NodeDef& resolveDef(const ::NodeInstance&);

// Pobla `inst.fields` con la Quantity por defecto de cada Field
// declarado en el def resuelto.  Se llama desde makeNode/makeCustomNode.
void seedFields(::NodeInstance& inst);

// ¿Es este nodo alias-like? — emite la misma señal/unit que el output
// de otro nodo, identificado por sus params `target_node_id` /
// `target_port`.  Devuelve `(targetId, targetPort)` si lo es y los
// params apuntan a un nodo válido; nullopt si no.
//
// Convención: el chequeo de "es alias" se hace por la PRESENCIA de
// `target_node_id` con valor positivo, además del NodeType.  Centraliza
// el "qué cuenta como alias" — los 3 consumidores (analyzer dimensional,
// topoSort, codegen) llaman a esta función en lugar de comparar
// `n.type == NodeType::Alias` cada uno y resolver los params a mano.
//
// Si en el futuro hay otro tipo alias-like (p. ej. AliasRef, ProxyNode),
// agregar el `NodeType` adentro de la función — un solo cambio cubre
// todos los call sites.
std::optional<std::pair<int, int>> aliasTargetOf(const ::NodeInstance& n);

}  // namespace scinodes
