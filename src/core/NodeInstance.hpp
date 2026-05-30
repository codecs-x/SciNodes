#pragma once
#include "NodeType.hpp"
#include "Quantity.hpp"
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Esquema de identificadores de atributo (ImGui/imnodes attribute IDs).
//
// Cada nodo reserva un bloque consecutivo de `kAttrIdNodeStride` ids
// derivados de su nodeId:
//
//   nodeId N reserva el rango [N·S, (N+1)·S)   donde S = kAttrIdNodeStride
//
//     inputs:  [N·S + 0,                       N·S + kAttrIdParamBase)
//                                              (port = attrId − N·S)
//     params:  [N·S + kAttrIdParamBase,        N·S + kAttrIdOutputBase)
//                                              (idx  = attrId − N·S − kAttrIdParamBase)
//     outputs: [N·S + kAttrIdOutputBase,       N·S + kAttrIdNodeStride)
//                                              (port = attrId − N·S − kAttrIdOutputBase)
//
// Decisión histórica: bandas por módulo en lugar de tabla de lookup.
// Permite distinguir input/param/output con aritmética modular barata
// y reservar capacidad sobrada (≤100 inputs/params, ≤1000 outputs por
// nodo) sin gastar memoria adicional.
//
// Los helpers libres en este header son el ÚNICO sitio donde aparece la
// aritmética del esquema.  Cualquier sitio que necesite "extraer el
// nodeId" o "saber si esto es un output" debe usar las funciones, no
// duplicar el `% 10000` ni el `>= 9000`.
// ---------------------------------------------------------------------------
constexpr int kAttrIdNodeStride = 10000;
constexpr int kAttrIdParamBase  = 100;
constexpr int kAttrIdOutputBase = 9000;
constexpr int kAttrIdOutputMax  = kAttrIdNodeStride - 1;   // 9999

// Decoders puros — toman un attrId y devuelven la información extraída.
// Todos son funciones libres `constexpr` para que el codegen y la UI
// puedan llamarlas sin overhead.
constexpr int  attrNodeId(int attrId)    { return attrId / kAttrIdNodeStride; }
constexpr int  attrLocal(int attrId)     { return attrId % kAttrIdNodeStride; }
constexpr bool attrIsOutput(int attrId)  {
    const int m = attrLocal(attrId);
    return m >= kAttrIdOutputBase && m <= kAttrIdOutputMax;
}
constexpr bool attrIsParam(int attrId)   {
    const int m = attrLocal(attrId);
    return m >= kAttrIdParamBase && m < kAttrIdOutputBase;
}
constexpr bool attrIsInput(int attrId)   {
    return attrLocal(attrId) < kAttrIdParamBase;
}
// Para un attrId conocido de un puerto: índice del puerto dentro del nodo.
constexpr int  attrInputPort(int attrId)  { return attrLocal(attrId); }
constexpr int  attrOutputPort(int attrId) { return attrLocal(attrId) - kAttrIdOutputBase; }
constexpr int  attrParamIdx(int attrId)   { return attrLocal(attrId) - kAttrIdParamBase; }

// Reescribe el nodeId de un attrId conservando su banda (input/param/output)
// y posición.  Lo usan flatten y encapsulate al re-cablear aristas cuando
// los nodos cambian de id pero conservan sus puertos.
constexpr int  attrRemap(int attrId, int newNodeId) {
    return newNodeId * kAttrIdNodeStride + attrLocal(attrId);
}

// Claves de portUnitOverrides — codifican un puerto sin nodeId.  Los
// dos rangos (input vs output) no colisionan porque usan distintos
// bases.  Forward-only — el deserializer simétrico vive en
// ScnSerializer y el analyzer lee con estos helpers.
constexpr int portKeyForInput (int portIdx) { return portIdx; }
constexpr int portKeyForOutput(int portIdx) { return kAttrIdOutputBase + portIdx; }

// Posición del nodo en el canvas.  Antes vivía en un side-table
// (`ScnPositions`) del NodeCanvas; ahora es parte del modelo para que
// el serializer pueda recorrerla recursivamente junto con los SubGraphs
// y las herramientas headless puedan razonar sobre layout sin GUI.
struct NodePos { float x = 0.f; float y = 0.f; };

// -----------------------------------------------------------------------
// NodeInstance — a single placed node in the graph
// -----------------------------------------------------------------------
struct NodeInstance {
    int      id;        // unique within the graph (sequential)
    NodeType type;

    // Only meaningful when type == NodeType::Custom. Identifies which
    // descriptor in CustomNodeRegistry this instance belongs to.
    std::string customType;

    // Parameter values indexed by ParamDef::name
    std::unordered_map<std::string, double> params;

    // Etapa 6I.D.1: representación unificada como Quantity (value + unit).
    // Convive con `params` durante la transición — `params[name]` es la
    // fuente para los 95 call sites del codegen / GUI / serializer; ESTA
    // tabla refleja lo mismo pero con la unidad declarada por el
    // NodeDef.FieldDef.defaultQuantity.  `setParam` mantiene
    // `fields[name].value == params[name]` automáticamente.
    //
    // La clave coincide con la de `params` para param fields ("Kp"), y
    // sigue el esquema "in0", "out0" para puertos (etapa 6I.B —
    // synthesizeFields).  Etapa 6I.D.2 invierte la dirección: fields se
    // vuelve fuente, params un proxy de `.value`.  6I.H borra `params`.
    std::unordered_map<std::string, scinodes::Quantity> fields;

    // Parámetros de tipo string indexados por clave libre — usados
    // por sinks multi-canal (Oscilloscope) para guardar metadata
    // editable: "portLabel0" = "Codo A — posición", "portUnit0" =
    // "rad", etc.  Persistidos en .scn.  Vacío para nodos que no
    // los usan.  Cualquier nodo puede grabar strings aquí; las
    // claves no están restringidas por NodeDef.
    std::unordered_map<std::string, std::string> stringParams;

    // Para nodos NodeCategory::Device: ruta (relativa al .scn o
    // absoluta) del asset glTF cargado y validado contra el contrato
    // del tipo.  Vacío significa "sin asset asignado" — la malla 3D
    // queda en blanco hasta que el usuario lo asigne desde la UI.
    // Persistido en el archivo .scn.
    std::string assetPath;

    // Comentario libre del usuario — el "por qué" detrás de elegir
    // este nodo aquí, qué representa en el sistema, o cualquier nota
    // pedagógica.  Vacío por defecto.  Se edita con F2 (junto con el
    // Name en SubGraphs) y se muestra como tooltip al `Ctrl + hover`
    // sobre el nodo.  Persistido en el .scn.
    std::string comment;

    // Per-instance unit overrides (etapa 6G del análisis dimensional).
    // Permite que el usuario marque un puerto POLIMÓRFICO (sin
    // declaración en el registry) con una unidad concreta — convierte
    // p. ej. un PID en unit-transformer "rad → V" sin tocar el código
    // del nodo.  Si el registry YA declara el puerto, el override se
    // ignora (los nodos canónicamente dimensionados no se corrompen).
    //
    // La key codifica input/output + índice:
    //   - inputs:  attrLocal = port index           (0..99)
    //   - outputs: attrLocal = kAttrIdOutputBase + port (9000..9999)
    //
    // El `DimensionalAnalyzer` consulta esta tabla en la fase de seed
    // para puertos sin declaración del registry.  Persistido en .scn.
    //
    // El valor se almacena como TEXTO (no `Unit` parseado) porque la
    // canonicalización SI pierde información del usuario para unidades
    // dimensionalmente equivalentes: p.ej. `rad` (adimensional × 1.0)
    // y "1" (dimensionless) producen el mismo `Unit{exp=0, mag=1}`, lo
    // que rompía el display y el round-trip .scn.  Manteniendo el texto
    // verbatim, el usuario ve exactamente lo que escribió y la
    // serialización es lossless.  El parser se invoca en analyze-time.
    std::unordered_map<int, std::string> portUnitOverrides;

    // Posición del nodo en el canvas (screen-space del editor context
    // del padre).  Cero por defecto; los handlers de UI la mantienen
    // sincronizada con imnodes en cada frame.  Persiste en .scn.
    NodePos position;

    // Para nodos type==SubGraph: conteo cacheado de puertos visibles
    // desde fuera, igual al número de `SubGraphInput`/`SubGraphOutput`
    // que existen en el grafo hijo.  `NodeGraph::recomputeSubGraphPorts`
    // los mantiene sincronizados cuando el contenido del subgrafo cambia.
    // `defOf()` los lee para sintetizar el `NodeDef` con el conteo
    // correcto; sin esto, el catálogo estático declara 0/0 puertos.
    int subGraphInputCount  = 0;
    int subGraphOutputCount = 0;

    // AttrIDs derivados del nodeId — encoding documentado arriba con
    // las constantes `kAttrIdNodeStride`, `kAttrIdParamBase`,
    // `kAttrIdOutputBase`.  Los helpers libres `attrIsInput`/
    // `attrIsOutput`/`attrNodeId`/etc. son los decoders.
    int inputAttrId(int port = 0)  const { return id * kAttrIdNodeStride + port; }
    int outputAttrId(int port = 0) const { return id * kAttrIdNodeStride + kAttrIdOutputBase + port; }
    int paramAttrId(int j)         const { return id * kAttrIdNodeStride + kAttrIdParamBase + j; }
};

// Construct a NodeInstance with default parameters from the registry.
NodeInstance makeNode(int id, NodeType type);

// Construct a NodeInstance for a JSON-defined node type. The descriptor
// must already be registered in CustomNodeRegistry; returns an instance
// with type == NodeType::Custom and customType == typeId. If typeId is
// unknown, the returned instance still has type == Custom but its params
// map is empty, and defOf() will fall through to a stub def.
NodeInstance makeCustomNode(int id, const std::string& typeId);

// Resolve a NodeInstance to a NodeDef that's safe for grammar/codegen to
// inspect. For builtin types this is just `nodeRegistry().at(n.type)`;
// for `Custom` instances the def is synthesized from CustomNodeRegistry
// and cached so the returned reference is stable.
const NodeDef& defOf(const NodeInstance& n);

// Instance-aware category lookup. Necessary because categoryOf(NodeType)
// has no entry for `Custom` — the answer depends on which JSON descriptor
// the instance points at.
NodeCategory categoryOf(const NodeInstance& n);
