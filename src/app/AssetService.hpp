#pragma once
#include "../core/AssetMapping.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/DeviceAsset.hpp"

#include <string>
#include <unordered_map>

namespace scinodes::app {

// ---------------------------------------------------------------------------
// AssetService — Facade (Gamma et al. / Ostrowski Cap. 8) sobre el subsistema
// de assets glTF de dispositivos físicos.
//
// Antes de C.8, NodeCanvas hacía inline: lookupear el contrato vía
// m_contractRegistry, llamar a DeviceAssetLoader::load(), guardar el
// DeviceAsset en un mapa por nodeId, y resolver sidecar paths via
// AssetMapping::sidecarPathFor.  Tres dependencias acopladas a UI.
//
// Esta clase encapsula esa colaboración detrás de tres operaciones simples:
//   • reload(nodeId, typeName, path)  — carga + valida y cachea.
//   • detach(nodeId)                  — borra del cache.
//   • find(nodeId)                    — lectura del cache.
//
// El cache es propiedad del service.  NodeCanvas conserva sólo una
// referencia.  La validación contra contrato es atómica con la carga: el
// caller no la ve por separado.
// ---------------------------------------------------------------------------
class AssetService {
public:
    explicit AssetService(const scinodes::ContractRegistry& contracts);

    // (Re)carga el asset del nodo.  `typeName` es el resultado de
    // typeName(NodeType) — se usa para resolver el contrato.  `path` es
    // la ruta del .gltf / .glb en disco.
    //
    // Retorna true si había contrato registrado para el tipo y la carga
    // fue intentada (success y missing se reflejan en el DeviceAsset
    // cacheado).  Retorna false si no hay contrato — en ese caso la
    // entrada del cache se borra (la UI mostrará "(sin contrato
    // registrado)").
    //
    // Si `path` está vacío, equivale a detach(nodeId).
    bool reload(int                nodeId,
                const std::string& typeName,
                const std::string& path);

    void detach(int nodeId);

    // El .scn guarda rutas de asset relativas (p.ej.
    // "examples/dc_motor/dc_motor.gltf").  Cuando el binario corre desde
    // un cwd distinto al repo root (build/, build/Debug, etc.) la ruta
    // no resuelve.  NodeCanvas::loadFromFile llama setBaseDir con el
    // directorio del .scn cargado; AssetService prueba ese directorio +
    // sus ancestros antes de fallar.
    void setBaseDir(const std::string& dir) { m_baseDir = dir; }
    const std::string& baseDir() const { return m_baseDir; }

    // Resuelve una assetPath relativa a una ruta absoluta que existe en
    // disco.  Si la ruta ya es absoluta y existe, la devuelve tal cual.
    // Si ningún candidato existe, devuelve la entrada original (el
    // caller obtendrá el error del loader como antes).
    std::string resolveAssetPath(const std::string& assetPath) const;

    // Lookup en cache.  nullptr si no hay entrada.
    const scinodes::DeviceAsset* find(int nodeId) const;

    // Lectura del cache completo — usada por View3DPanel y el Outliner
    // para enumerar todos los assets cargados.
    const std::unordered_map<int, scinodes::DeviceAsset>& all() const {
        return m_cache;
    }

    // Helpers de sidecar (mapping JSON).  La fachada los expone para que
    // los callers no tengan que importar AssetMapping directamente.
    static std::string sidecarPathFor(const std::string& assetPath);
    static bool        sidecarExists(const std::string& assetPath);

    // Acceso al registry de contratos — útil para code paths que
    // necesitan el DeviceContract* además del asset (p. ej. apertura del
    // panel de mapping requiere el contrato).
    const scinodes::ContractRegistry& contracts() const { return m_contracts; }

private:
    const scinodes::ContractRegistry&                m_contracts;
    std::unordered_map<int, scinodes::DeviceAsset>   m_cache;
    std::string                                      m_baseDir;
};

}  // namespace scinodes::app
