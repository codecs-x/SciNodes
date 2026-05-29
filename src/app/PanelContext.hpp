#pragma once

#include "../core/ContractRegistry.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"

#include <unordered_map>

class NodeCanvas;   // fwd

// -----------------------------------------------------------------------------
// IPanelContext — abstracción de las dependencias compartidas que los
// panel-adapters necesitan para hacer su trabajo: el grafo activo, el
// bridge de simulación, y el cache de assets 3D cargados.
//
// Motivación (DIP, Martin Cap 11 / Ostrowski Cap 3): los adapters
// concretos antes tomaban refs sueltas a NodeCanvas + ScilabBridge,
// acoplándose a tipos concretos en vez de a una abstracción.  La
// interfaz invierte la dependencia: AppWindow implementa IPanelContext
// y los adapters se construyen contra él.  Si en el futuro hace falta
// testear un panel sin GUI, basta con dar una implementación
// mock-friendly de IPanelContext.
//
// Convención: el método `canvas()` devuelve un NodeCanvas concreto
// porque algunos paneles (Outliner) necesitan llamar métodos
// específicos del canvas (`reloadAsset`, `detachAsset`).  Eso rompe
// estrictamente DIP en ese sentido, pero la alternativa sería un
// IPanelContext con 8 métodos en vez de 3 — pragmatismo.
// -----------------------------------------------------------------------------
namespace scinodes::app {

class IPanelContext {
public:
    virtual ~IPanelContext() = default;

    virtual const NodeGraph& graph() const = 0;
    virtual ScilabBridge&    bridge() = 0;
    virtual const std::unordered_map<int, scinodes::DeviceAsset>&
                             loadedAssets() const = 0;

    // Catálogo de contratos device.  Antes vivía como singleton
    // ContractRegistry::instance(); ahora se inyecta aquí (DIP).
    virtual const scinodes::ContractRegistry& contractRegistry() const = 0;

    // Acceso completo al canvas — algunos paneles (Outliner) modifican
    // estado canvas-local.  Mantener pequeño y bien acotado.
    virtual NodeCanvas& canvas() = 0;
};

// -----------------------------------------------------------------------------
// PanelContext — implementación concreta para producción.  Mantiene
// refs no-owning al NodeCanvas y al ScilabBridge que AppWindow posee.
// Los getters delegan al canvas/bridge.
// -----------------------------------------------------------------------------
class PanelContext : public IPanelContext {
public:
    PanelContext(NodeCanvas&                       canvas,
                 ScilabBridge&                     bridge,
                 const scinodes::ContractRegistry& contracts)
        : m_canvas(canvas), m_bridge(bridge), m_contracts(contracts) {}

    const NodeGraph& graph() const override;
    ScilabBridge&    bridge() override { return m_bridge; }
    const std::unordered_map<int, scinodes::DeviceAsset>&
                     loadedAssets() const override;
    const scinodes::ContractRegistry& contractRegistry() const override {
        return m_contracts;
    }
    NodeCanvas&      canvas() override { return m_canvas; }

private:
    NodeCanvas&                       m_canvas;
    ScilabBridge&                     m_bridge;
    const scinodes::ContractRegistry& m_contracts;
};

}  // namespace scinodes::app
