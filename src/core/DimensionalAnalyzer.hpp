#pragma once

#include "NodeGraph.hpp"
#include "Unit.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace scinodes {

// ---------------------------------------------------------------------------
// DimensionalAnalysis — resultado de propagar unidades por el grafo.
// Producido por `analyzeUnits(graph)`.  Es la fundación que R7 (etapa 6F)
// consume para aceptar/rechazar edges.  Ver
// `doc/designs/dimensional_analysis_proposal.md` v2 §5–§6.
//
// El análisis recorre el grafo dos veces:
//
//   1. Inicialización: cada puerto declarado por el registry
//      (NodeDef::inputPortUnits / outputPortUnits) entra al mapa
//      `inferred` con su unidad fija.
//   2. Punto fijo: propaga unidades por edges (ambos extremos
//      comparten unit) y por nodos polimórficos (todos los puertos
//      del nodo comparten unit) hasta que ningún cambio ocurre.
//   3. Conflictos: cuando un puerto recibe DOS unidades distintas
//      (forward & backward, o dos edges convergiendo, o un nodo
//      polimórfico con puertos incompatibles), se registra un
//      Conflict.
//
// "Polimórfico" en este contexto significa "el nodo no declara
// unidades en ningún puerto" — sus ports unifican entre sí.  Un nodo
// con CUALQUIER declaración (parcial o total) deja sus puertos
// independientes; la propagación sólo cruza por edges, no por la
// estructura interna del nodo.  Esto encaja con cómo está poblado
// el registry hoy: Gain/Sum/Integrator/Step son polimórficos totales;
// VoltageSource/DCMotor/GearTransmission son dimensionados totales.
//
// Custom nodes y SubGraphs se tratan como opacos (sus declaraciones
// del descriptor JSON o de su contenido interno NO se inspeccionan
// — quedan polimórficos al nivel del análisis).
// ---------------------------------------------------------------------------
struct DimensionalAnalysis {
    // attrId → unidad inferida/declarada.  Si una clave falta, el
    // puerto es polimórfico sin contexto suficiente — su unidad
    // queda como "?" en la UI.
    std::unordered_map<int, Unit> inferred;

    struct Conflict {
        int         attrId;     // puerto en conflicto
        std::string message;    // descripción del dominio del problema
    };
    std::vector<Conflict> conflicts;

    bool isResolved(int attrId) const {
        return inferred.count(attrId) > 0;
    }

    Unit unitAt(int attrId) const {
        auto it = inferred.find(attrId);
        return (it == inferred.end()) ? Unit{} : it->second;
    }

    bool ok() const { return conflicts.empty(); }
};

// Recorre el grafo y resuelve las unidades en cada puerto.  Pure
// function — no muta el grafo ni guarda estado entre llamadas.
DimensionalAnalysis analyzeUnits(const NodeGraph& graph);

}  // namespace scinodes
