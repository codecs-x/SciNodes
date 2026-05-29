#pragma once
#include "NodeType.hpp"
#include <string>
#include <unordered_map>

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

    // imnodes attribute IDs derived from node id (multiplier = 10000):
    //   input  port i → id * 10000 + i              (i = 0, 1, …)
    //   output port k → id * 10000 + 9000 + k       (k = 0, 1, …)
    //   param  attrs  → id * 10000 + 100 + j        (display-only, not linked)
    //
    // Detection helpers (given any attr id):
    //   node id  = attrId / 10000
    //   is output: attrId % 10000 >= 9000     (range 9000..9999)
    //   is input:  attrId % 10000 < 100
    int inputAttrId(int port = 0)  const { return id * 10000 + port; }
    int outputAttrId(int port = 0) const { return id * 10000 + 9000 + port; }
    int paramAttrId(int j)         const { return id * 10000 + 100 + j; }
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
