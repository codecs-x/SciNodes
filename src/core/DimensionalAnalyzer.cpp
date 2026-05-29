#include "DimensionalAnalyzer.hpp"
#include "NodeInstance.hpp"
#include "NodeType.hpp"

namespace scinodes {

namespace {

// ¿El nodo es POLIMÓRFICO completo? — sin ninguna declaración de
// unidad en sus puertos NI overrides per-instance.  En ese caso todos
// sus puertos comparten unit (propagación interna al nodo).
// Cualquier declaración (registry o instance override) rompe la
// simetría y los puertos quedan independientes — la conexión sólo
// cruza por edges, no por estructura del nodo.
//
// Justificación: ningún nodo del registry actual usa la modalidad
// "mixto" (algunos puertos declarados, otros no).  El override
// per-instance ES el mecanismo para convertir un polimórfico en
// unit-transformer (PID: rad → V).  Con cualquier override presente,
// el nodo deja de propagar internamente.
bool isFullyPolymorphic(const NodeInstance& n, const NodeDef& def) {
    return def.inputPortUnits.empty()
        && def.outputPortUnits.empty()
        && n.portUnitOverrides.empty()
        // Etapa 6I.O: unit-transformers (Integrator/Differentiator) no son
        // polimórficos — su out se deriva de in × graph.domainUnit().
        && def.unitTransformKind == NodeDef::UnitTransformKind::None;
}

// Registra un conflicto sin duplicar mensajes idénticos sobre el
// mismo attrId — evita ruido en la salida cuando un mismo conflicto
// se descubre por dos caminos.
void addConflict(DimensionalAnalysis& a, int attrId, std::string msg) {
    for (const auto& c : a.conflicts) {
        if (c.attrId == attrId && c.message == msg) return;
    }
    a.conflicts.push_back({ attrId, std::move(msg) });
}

// Intenta asignar `u` al `attrId`.  Si ya existe y la dimensión NO
// coincide, registra conflicto.  Devuelve true si esto cambió el
// estado (asignación nueva) — el caller usa esto para detectar
// fixed-point.
bool assignUnit(DimensionalAnalysis& a,
                int attrId,
                const Unit& u,
                const std::string& source) {
    auto it = a.inferred.find(attrId);
    if (it == a.inferred.end()) {
        a.inferred[attrId] = u;
        return true;
    }
    if (!it->second.sameDimension(u)) {
        std::string msg = "Dimensional conflict (" + source + "): expected "
                        + it->second.toCanonicalString() + ", got "
                        + u.toCanonicalString();
        addConflict(a, attrId, std::move(msg));
    }
    return false;
}

}  // anon

DimensionalAnalysis analyzeUnits(const NodeGraph& graph) {
    DimensionalAnalysis result;

    // ---- 1. Seed: unidades DECLARADAS del registry + OVERRIDES per-
    //         instance (etapa 6G).  Los overrides se aplican SOLO a
    //         puertos no declarados en el registry — los nodos
    //         canónicamente dimensionados (VoltageSource, DCMotor)
    //         son inmunes a corrupción por override.
    for (const NodeInstance& n : graph.nodes()) {
        const NodeDef& def = defOf(n);
        for (int i = 0; i < (int)def.inputPortUnits.size(); ++i) {
            assignUnit(result, n.inputAttrId(i),
                       def.inputPortUnits[i],
                       "declared input " + std::to_string(i)
                         + " of " + def.label);
        }
        for (int o = 0; o < (int)def.outputPortUnits.size(); ++o) {
            assignUnit(result, n.outputAttrId(o),
                       def.outputPortUnits[o],
                       "declared output " + std::to_string(o)
                         + " of " + def.label);
        }
        // Overrides per-instance — sólo donde el registry no declaró.
        // El override se guarda como TEXTO; lo parseamos aquí.  Texto
        // inválido se ignora silenciosamente (la UI ya rechaza commits
        // que no parsean, así que sólo entran .scn corruptos manualmente).
        for (int i = 0; i < def.inputPorts; ++i) {
            if (hasDeclaredInputUnit(def, i)) continue;
            auto it = n.portUnitOverrides.find(portKeyForInput(i));
            if (it == n.portUnitOverrides.end()) continue;
            auto pr = scinodes::parseUnit(it->second);
            if (!pr.ok()) continue;
            assignUnit(result, n.inputAttrId(i), pr.unit,
                       "instance override input " + std::to_string(i)
                         + " of " + def.label);
        }
        for (int o = 0; o < def.outputPorts; ++o) {
            if (hasDeclaredOutputUnit(def, o)) continue;
            auto it = n.portUnitOverrides.find(portKeyForOutput(o));
            if (it == n.portUnitOverrides.end()) continue;
            auto pr = scinodes::parseUnit(it->second);
            if (!pr.ok()) continue;
            assignUnit(result, n.outputAttrId(o), pr.unit,
                       "instance override output " + std::to_string(o)
                         + " of " + def.label);
        }
    }

    // ---- 2. Punto fijo: propagar por edges y nodos polimórficos. ---
    // La iteración corta cuando una pasada completa no agrega ningún
    // attrId nuevo a `inferred` ni descubre conflictos nuevos.
    bool changed = true;
    int  maxIter = 32;   // safety cap — grafos típicos convergen en 2-3
    while (changed && maxIter-- > 0) {
        changed = false;

        // 2a. Edges: ambos extremos comparten unit.
        for (const Edge& e : graph.edges()) {
            auto from = result.inferred.find(e.fromAttrId);
            auto to   = result.inferred.find(e.toAttrId);
            const bool fromKnown = from != result.inferred.end();
            const bool toKnown   = to   != result.inferred.end();
            if (fromKnown && toKnown) {
                if (!from->second.sameDimension(to->second)) {
                    std::string msg = "Edge dimensional mismatch: "
                                    + from->second.toCanonicalString()
                                    + " → "
                                    + to->second.toCanonicalString();
                    addConflict(result, e.toAttrId, std::move(msg));
                }
            } else if (fromKnown && !toKnown) {
                if (assignUnit(result, e.toAttrId, from->second,
                              "edge propagation")) changed = true;
            } else if (!fromKnown && toKnown) {
                if (assignUnit(result, e.fromAttrId, to->second,
                              "edge propagation backward")) changed = true;
            }
        }

        // 2a'. Unit-transformer (etapa 6I.K + 6I.O): nodos cuya
        // relación dimensional depende del DOMINIO del grafo
        // (graph.domainUnit()).  Para time-domain (default s):
        //   MultiplyDomain  → out = in × s   (Integrator: rad/s → rad)
        //   DivideDomain    → out = in / s   (Differentiator: rad → rad/s)
        // Para un futuro modo frequency-domain o espacial, basta cambiar
        // graph.setDomainUnit() — el algoritmo es el mismo.
        for (const NodeInstance& n : graph.nodes()) {
            const NodeDef& def = defOf(n);
            if (def.unitTransformKind == NodeDef::UnitTransformKind::None) continue;
            if (def.inputPorts != 1 || def.outputPorts != 1) continue;
            Unit factor;
            switch (def.unitTransformKind) {
                case NodeDef::UnitTransformKind::MultiplyDomain:
                    factor = graph.domainUnit();
                    break;
                case NodeDef::UnitTransformKind::DivideDomain:
                    factor = unitDimensionless() / graph.domainUnit();
                    break;
                default: continue;
            }
            const int  inAttr  = n.inputAttrId(0);
            const int  outAttr = n.outputAttrId(0);
            auto itIn  = result.inferred.find(inAttr);
            auto itOut = result.inferred.find(outAttr);
            const bool inKnown  = itIn  != result.inferred.end();
            const bool outKnown = itOut != result.inferred.end();
            if (inKnown && !outKnown) {
                if (assignUnit(result, outAttr, itIn->second * factor,
                              "unit-transformer forward (" + def.label + ")"))
                    changed = true;
            } else if (!inKnown && outKnown) {
                if (assignUnit(result, inAttr, itOut->second / factor,
                              "unit-transformer backward (" + def.label + ")"))
                    changed = true;
            } else if (inKnown && outKnown) {
                Unit expected = itIn->second * factor;
                if (!expected.sameDimension(itOut->second)) {
                    std::string msg = "Unit-transformer mismatch: expected "
                                    + expected.toCanonicalString() + " on output, got "
                                    + itOut->second.toCanonicalString();
                    addConflict(result, outAttr, std::move(msg));
                }
            }
        }

        // 2b. Nodos polimórficos: todos sus puertos comparten unit.
        for (const NodeInstance& n : graph.nodes()) {
            const NodeDef& def = defOf(n);
            if (!isFullyPolymorphic(n, def)) continue;

            // Junta todos los attrIds del nodo (inputs + output).
            std::vector<int> ports;
            ports.reserve(def.inputPorts + def.outputPorts);
            for (int i = 0; i < def.inputPorts;  ++i)
                ports.push_back(n.inputAttrId(i));
            for (int o = 0; o < def.outputPorts; ++o)
                ports.push_back(n.outputAttrId(o));

            // Busca CUALQUIER unidad ya conocida; si dos conocidas
            // disagree, conflicto en el nodo.
            const Unit* uref = nullptr;
            int          urefAttr = 0;
            for (int aid : ports) {
                auto it = result.inferred.find(aid);
                if (it == result.inferred.end()) continue;
                if (!uref) { uref = &it->second; urefAttr = aid; }
                else if (!uref->sameDimension(it->second)) {
                    std::string msg = "Polymorphic node \"" + def.label
                                    + "\" has incompatible units on ports: "
                                    + uref->toCanonicalString() + " vs "
                                    + it->second.toCanonicalString();
                    addConflict(result, aid, std::move(msg));
                }
            }
            // Si encontramos una unidad, expandirla a los puertos sin
            // resolver.
            if (uref) {
                Unit u = *uref;   // copia para no perder referencia
                for (int aid : ports) {
                    if (!result.inferred.count(aid)) {
                        result.inferred[aid] = u;
                        changed = true;
                    }
                }
            }
        }
    }

    return result;
}

}  // namespace scinodes
