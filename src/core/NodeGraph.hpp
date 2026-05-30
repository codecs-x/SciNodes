#pragma once
#include "Edge.hpp"
#include "GrammarParser.hpp"
#include "NodeInstance.hpp"
#include "Quantity.hpp"
#include "UndoRedoStack.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------
// ImportedObject — catálogo a nivel proyecto de objetos 3D importados.
// Vive como metadata root del NodeGraph top-level (no en SubGraphs
// anidados).  Los nodos `Object3D` referencian al catálogo por
// `objectRef` = "<name>/<partName>" — desacopla la geometría del
// cómputo (ver `doc/3d_scene_graph_design.md` §5).
// -----------------------------------------------------------------------
struct ImportedObject {
    std::string              name;   // visible al usuario, ej. "Motor DC"
    std::string              path;   // ruta al .gltf (relativa al .scn o absoluta)
    std::vector<std::string> parts;  // sub-partes del modelo (ej. shaft, housing)
};

// -----------------------------------------------------------------------
// NodeGraph — owns nodes + edges, validates via GrammarParser.
//
// All mutations go through this class so grammar invariants are
// maintained and snapshots are always consistent.
// -----------------------------------------------------------------------
class NodeGraph {
public:
    // ---- node management -------------------------------------------------
    int  addNode(NodeType type);          // returns new node ID
    int  addCustomNode(const std::string& customType);  // JSON-loaded type
    // Variantes que preservan un ID explícito (encapsulate las usa para
    // mantener los IDs originales dentro del SubGraph nuevo — sin esto,
    // el codegen renumeraría los slots de estado y un Resume tras
    // encapsular perdería todos los valores acumulados).  El caller
    // garantiza que el ID no colisione en este grafo; m_nextNodeId
    // se actualiza para mantenerlo más allá del más alto observado.
    int  addNodeWithId(NodeType type, int id);
    int  addCustomNodeWithId(const std::string& customType, int id);
    void removeNode(int nodeId);          // also removes incident edges

    // ---- edge management -------------------------------------------------
    // Returns nullopt on success; GrammarError if the edge is invalid.
    std::optional<GrammarError>
    tryAddEdge(int fromAttrId, int toAttrId);

    void removeEdge(int edgeId);

    // ---- accessors -------------------------------------------------------
    const std::vector<NodeInstance>& nodes() const { return m_nodes; }
    const std::vector<Edge>&         edges() const { return m_edges; }

    int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    int edgeCount() const { return static_cast<int>(m_edges.size()); }

    const NodeInstance* findNode(int nodeId)  const;
    const Edge*         findEdge(int edgeId)  const;

    // Modify a live parameter value (does not validate grammar).
    // No-op if nodeId or name is not found.
    void setParam(int nodeId, const std::string& name, double value);

    // Etapa 6I.F: setter unificado para Quantity.  Actualiza
    // fields[name] entera (value + unit) y mantiene params[name] en
    // sync con .value para los call sites legacy.  Si el field no
    // existía, lo crea (caso de overrides sobre puertos polimórficos).
    // Es el setter que usa el QuantityField widget al commit.
    void setFieldQuantity(int nodeId, const std::string& name,
                          scinodes::Quantity q);

    // Set/insert a string-valued metadata entry on the node (etiquetas
    // editables por puerto del Oscilloscope, etc.).  Persistido en .scn.
    void setStringParam(int nodeId, const std::string& key,
                        const std::string& value);

    // Modify the asset path of a Device node (no-op para no-Device).
    // Vacío significa "sin asset asignado".
    void setAssetPath(int nodeId, const std::string& path);

    // Set the user-authored comment on a node (texto libre).  Vacío =
    // sin comentario.  Persistido en .scn.
    void setComment(int nodeId, const std::string& text);

    // Per-instance unit overrides (etapa 6G).  `key` codifica
    // input/output + index vía `portKeyForInput`/`portKeyForOutput`.
    // setPortUnitOverride sobrescribe; clear borra la entrada.  Si el
    // registry ya declaró el puerto, el override se guarda igualmente
    // pero el analyzer lo ignora — los nodos canónicos son inmunes.
    void setPortUnitOverride  (int nodeId, int key, std::string text);
    void clearPortUnitOverride(int nodeId, int key);

    // ---- root metadata (top-level .scn) ---------------------------------
    // Id, title, description y tags describen el grafo entero como
    // pieza de investigación: viven con el .scn, no en un manifiesto
    // aparte.  Es el nivel donde habita el "¿de qué trata este
    // experimento?" — análogo al comentario de un nodo pero para el
    // grafo completo.  Sólo el grafo de top-level los emite/lee al
    // serializar; los SubGraphs anidados los ignoran (su título lo da
    // el `Name` del nodo padre y su comentario el campo `comment`).
    //
    // El `id` es un identificador estable y corto (ej. "E1", "pid_2")
    // que sobrevive a renombrados del archivo.  Si está vacío, el
    // cliente (LinearExampleLibrary) cae al stem del filename.
    const std::string&              id()          const { return m_id; }
    const std::string&              title()       const { return m_title; }
    const std::string&              description() const { return m_description; }
    const std::vector<std::string>& tags()        const { return m_tags; }

    void setId         (std::string s);
    void setTitle      (std::string s);
    void setDescription(std::string s);
    void setTags       (std::vector<std::string> v);

    // ---- domain unit del grafo (etapa 6I.O) -----------------------------
    // El "dominio" del simulador determina respecto a qué variable
    // integramos.  Para SciNodes (time-domain) es `s` (segundos).
    // Para un eventual modo Fourier sería `Hz`/`rad/s`; para un
    // espacial sería `m`.  El Integrator/Differentiator usan ESTE
    // factor — no uno hardcoded — al propagar unidades.  Cambiar el
    // domain implica re-correr el solver (no es hot-swappable).
    const scinodes::Unit& domainUnit() const { return m_domainUnit; }
    void setDomainUnit(scinodes::Unit u) { m_domainUnit = std::move(u); }

    // ---- display units del proyecto (etapa 6I.C) -----------------------
    // El usuario elige UNIDAD PREFERIDA POR DIMENSIÓN — análogo al panel
    // "Units" de Blender/CAD.  Si el grafo declara `displayUnits[m]`,
    // todo Quantity con dimensión de longitud (cm, km, mm) se renderiza
    // convertido a m.  El storage interno del Quantity NO cambia — la
    // canonicalización corre sólo al display.
    //
    // Indexado por la firma SI (array de 7 exponentes) — la "dimensión"
    // física.  Dos unidades con misma firma se consideran intercambiables
    // (V y mV, ambas voltage; m y cm, ambas length).  Para dimensions
    // adimensionales con magnitud distinta (rad vs deg) hace falta una
    // entrada separada (futuro 6I.C.2 — Option B).
    //
    // Sólo el grafo top-level lo emite/lee; SubGraphs anidados heredan
    // del padre y no llevan su propio mapa.  Persistido en .scn:
    //   "display_units": ["m", "V", "Hz"]
    using DimensionKey = std::array<int8_t, 8>;
    const std::map<DimensionKey, scinodes::Unit>& displayUnits() const {
        return m_displayUnits;
    }
    void setDisplayUnit  (scinodes::Unit u);
    void clearDisplayUnit(DimensionKey dim);

    // Aplica el override al valor del Quantity, devolviendo uno con la
    // unidad preferida del proyecto (si existe entry para esa dim).
    // Sin entry → devuelve `q` sin cambios.  Conversión:
    //   newValue = q.value * (q.unit.magnitude / pref.magnitude)
    //   newUnit  = pref
    // Ej: q = {100, cm}, pref = m → returns {1, m}.
    scinodes::Quantity canonicalizeForDisplay(scinodes::Quantity q) const;

    // ---- imported-object catalog (proyecto) ----------------------------
    // Catálogo top-level de modelos 3D importados (.gltf) — los nodos
    // `Object3D` referencian sus partes por nombre (`"<obj>/<part>"`).
    // Sólo el grafo de top-level los emite/lee; los SubGraphs anidados
    // no llevan catálogo propio.  Los setters NO disparan undo todavía
    // — al igual que id/title/description/tags viven fuera del
    // GraphSnapshot.  Persistidos en .scn ("objects" en la raíz).
    const std::vector<ImportedObject>& importedObjects() const { return m_objects; }
    void addImportedObject   (ImportedObject o);
    void removeImportedObject(const std::string& name);   // no-op si no existe
    void setImportedObjects  (std::vector<ImportedObject> v);
    const ImportedObject* findImportedObject(const std::string& name) const;

    // ---- grammar ---------------------------------------------------------
    GrammarState grammarState()     const;
    const char*  grammarLabel()     const;

    // R7 (etapa 6F del análisis dimensional): si está activo, tryAddEdge
    // rechaza edges que introducen conflictos dimensionales nuevos.  El
    // toggle vive aquí porque el registry actual sólo declara unidades
    // en un subconjunto de nodos (VoltageSource, DCMotor, GearTransmission);
    // hasta que etapa 6G provea unit-overrides per-instance para
    // polimórficos (PID, Sum), los lazos de control típicos pueden
    // disparar falsos positivos.
    //
    // Default false — el registry actual sólo declara unidades en
    // VoltageSource, CurrentSource, DCMotor, GearTransmission.  Los
    // patrones típicos de control (Sum + PID + Motor con feedback)
    // disparan R7 porque Sum/PID son polimórficos y propagan unidades
    // incompatibles desde el feedback.  Etapa 6G (per-instance unit
    // overrides) habilitará declararlos como unit-transformers; recién
    // entonces conviene flip default a true.  Por ahora, opt-in via
    // `setDimensionalEnforcement(true)`.
    void setDimensionalEnforcement(bool on) { m_dimEnforce = on; }
    bool isDimensionalEnforcementOn() const { return m_dimEnforce; }

    // ---- SubGraph (SuperBlock) -------------------------------------------
    // Cada nodo de tipo `SubGraph` lleva asociado un grafo hijo recursivo
    // que vive en este side-table.  El nodo padre sólo guarda su id; el
    // contenido está aquí para que `NodeInstance` siga siendo trivialmente
    // copiable.  Las APIs:
    //
    //   addSubGraphNode()    → addNode(SubGraph) + crea grafo hijo vacío
    //   subGraphOf(id)       → puntero al grafo hijo (nullptr si no existe)
    //
    // El grafo hijo arranca con un `SubGraphInput` y un `SubGraphOutput`
    // por defecto (puertos 0/0); el llamador puede mutarlo libremente.
    int  addSubGraphNode();
    NodeGraph*       subGraphOf(int nodeId);
    const NodeGraph* subGraphOf(int nodeId) const;

    // Inserta un grafo hijo bajo `subGraphNodeId` (que debe existir en
    // este grafo como nodo SubGraph), reemplazando cualquier contenido
    // previo.  Usado por el serializer para restaurar SubGraphs
    // anidados desde .scn sin pasar por addSubGraphNode (que crearía
    // stubs default no deseados).
    void installSubGraph(int subGraphNodeId, NodeGraph&& child);

    // Re-cuenta los `SubGraphInput`/`SubGraphOutput` del grafo hijo y los
    // refleja en `NodeInstance::subGraphInputCount/outputCount` del nodo
    // padre.  Llamar tras cualquier mutación del subgrafo para que
    // `defOf()` retorne el conteo de puertos correcto.
    void recomputeSubGraphPorts(int subGraphNodeId);

    // Encapsulate (Ctrl+G headless).  Toma una lista explícita de nodeIds,
    // mueve esos nodos a un nuevo SubGraph, materializa stubs internos
    // para cada arista que cruza la frontera, y re-cablea las externas
    // por los puertos del SubGraph.
    //
    // Esta API es PURA: no consulta GUI ni mantiene posiciones.  El
    // adaptador `NodeCanvas` recoge los IDs de imnodes y delega aquí,
    // luego sincroniza posiciones por su cuenta usando `idMap`.
    struct EncapsulateResult {
        int sgId = 0;                                 // 0 = error
        std::unordered_map<int, int> idMap;           // oldId (padre) → newId (child)
    };
    EncapsulateResult encapsulateByIds(const std::vector<int>& ids);

    // ---- snapshot (for UndoRedoStack) ------------------------------------
    GraphSnapshot snapshot()                       const;
    void          restoreSnapshot(const GraphSnapshot& s);

private:
    // used only by restoreSnapshot — bypasses validation
    void restoreEdge(const Edge& e);

    std::vector<NodeInstance> m_nodes;
    std::vector<Edge>         m_edges;
    int m_nextNodeId = 1;
    int m_nextEdgeId = 1;

    bool m_dimEnforce = false;  // R7 enforcement toggle (etapa 6F); ver setter.

    // Root metadata — solo poblada en el grafo de top-level.
    std::string              m_id;
    std::string              m_title;
    std::string              m_description;
    std::vector<std::string> m_tags;
    std::vector<ImportedObject> m_objects;

    // Display-unit preferences indexed by SI dim signature (etapa 6I.C).
    std::map<DimensionKey, scinodes::Unit> m_displayUnits;

    // Etapa 6I.O: dominio del simulador.  Default = s (time-domain).
    scinodes::Unit m_domainUnit = scinodes::unitSecond();

    // SubGraph contents indexed by the parent SubGraph node's id.  The
    // value is a shared_ptr so NodeGraph remains copyable (snapshots
    // capture a deep clone via the explicit copy ctor below).
    std::map<int, std::shared_ptr<NodeGraph>> m_subGraphs;

    GrammarParser m_parser;

public:
    // Deep copy: subgraphs are cloned recursively so snapshots are
    // independent of the live graph.  Defaulted copy would share the
    // shared_ptr, which would break undo for SubGraph contents.
    NodeGraph();
    NodeGraph(const NodeGraph& other);
    NodeGraph& operator=(const NodeGraph& other);
    NodeGraph(NodeGraph&&) noexcept            = default;
    NodeGraph& operator=(NodeGraph&&) noexcept = default;
    ~NodeGraph()                               = default;
};
