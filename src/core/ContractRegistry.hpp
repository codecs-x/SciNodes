#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------------
// ContractRegistry — catálogo de contratos de dispositivos físicos.
//
// Un "contrato" describe qué partes, juntas cinemáticas y puntos de anclaje
// debe publicar un modelo 3D para ser considerado de un cierto tipo
// (DCMotor, PMSM, LinearActuator, ...).  La autoría del modelo vive afuera
// (Blender, FreeCAD); SciNodes solo consume y valida.
//
// El registry se carga desde una carpeta `contracts/` al iniciar, mismo
// patrón que CustomNodeRegistry.  Añadir un nuevo tipo de dispositivo
// requiere solo agregar un .json — no se recompila el binario.
//
// Spec completa: doc/designs/geometry-contracts-design.md
// -----------------------------------------------------------------------------
namespace scinodes {

struct ContractPart {
    std::string name;       // p.ej. "shaft", "housing"
    std::string kind;       // por ahora solo "mesh"
    bool        required = true;
    std::string doc;        // ayuda para el autor del asset
};

struct ContractJoint {
    std::string name;       // p.ej. "shaft_bearing"
    std::string type;       // "revolute" | "prismatic" | "fixed" |
                            // "cylindrical" | "ball" | "planar"
    std::string parent;     // nombre de una part del mismo contrato
    std::string child;      // idem
    std::string driven_by;  // nombre de variable de salida del nodo
                            // (ej. "omega" para revolute)
    bool        required = true;
    std::string doc;
};

struct ContractAnchor {
    std::string name;       // p.ej. "terminal_plus"
    std::string kind;       // "electrical" | "thermal_zone" |
                            // "force_application" | "mount"
    bool        required = true;
    std::string doc;
};

// Una entrada del registry.  device_type es la clave única que un nodo
// usa para localizar su contrato.
struct DeviceContract {
    std::string                  device_type;
    std::string                  version;       // p.ej. "0.1"
    std::string                  description;
    std::vector<ContractPart>    parts;
    std::vector<ContractJoint>   joints;
    std::vector<ContractAnchor>  anchors;
};

class ContractRegistry {
public:
    // Por defecto vacío.  El owner (AppWindow) carga al inicio con
    // loadFromDirectory("contracts/").  Para tests, se instancia
    // directamente y se prueba con loadFromJsonString.  Antes era
    // singleton via `instance()`; se eliminó para permitir DI
    // limpio y testing aislado (Phase C.7 — Martin Clean Architecture
    // Cap 11 DIP, Ostrowski Cap 3).
    ContractRegistry() = default;

    // Parsea un único JSON y registra el contrato.  Devuelve true en
    // éxito.  En fallo, *err (si no es nulo) lleva la razón y el registry
    // queda sin tocar.
    bool loadFromJsonString(const std::string& json,
                            std::string*       err = nullptr);

    // Idem desde archivo en disco.
    bool loadFromFile(const std::string& path,
                      std::string*       err = nullptr);

    // Carga todos los .json de una carpeta.  Llamado típicamente una vez
    // al iniciar la app con "contracts/".  Devuelve cuántos contratos
    // cargó.  Si algún archivo falla, su error se acumula en *err (los
    // demás siguen cargándose).
    int  loadFromDirectory(const std::string& dir,
                           std::string*       err = nullptr);

    // Lookup por device_type.  nullptr si no existe.
    const DeviceContract* find(const std::string& deviceType) const;

    // Lista de device_types registrados (orden no garantizado).
    std::vector<std::string> deviceTypes() const;

    // Wipe completo (mayormente para tests).
    void clear();

private:
    std::unordered_map<std::string, DeviceContract> m_contracts;
};

}  // namespace scinodes
