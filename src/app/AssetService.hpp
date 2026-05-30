#pragma once
#include "../core/AssetMapping.hpp"
#include "../core/ContractRegistry.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/SceneCollector.hpp"   // ISceneAssetResolver

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
class AssetService : public scinodes::ISceneAssetResolver {
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

    // ---- catálogo by-name (paso 5b del refactor 3D) --------------------
    // Cache paralelo al de m_cache (por nodeId), keyed por el `name` del
    // ImportedObject que vive en NodeGraph::importedObjects().  Es el
    // back-end concreto de `ISceneAssetResolver::resolveByName()` — el
    // walker del SceneCollector lo consulta para cada Object3D.
    //
    // La carga real (parsing .gltf sin contrato) la hace el caller
    // (paso 6: Menú Archivo → Importar) y la deposita aquí con
    // installNamedAsset.  El catálogo del NodeGraph y este cache se
    // mantienen separados: el catálogo es metadata persistida del
    // proyecto, el cache es estado en memoria del proceso vivo.
    void installNamedAsset(const std::string& name, scinodes::DeviceAsset asset);
    void detachNamed     (const std::string& name);
    bool hasNamed        (const std::string& name) const;

    // ISceneAssetResolver override.  Consulta m_namedAssets; nullptr si
    // el nombre no está cargado todavía.
    const scinodes::DeviceAsset*
    resolveByName(const std::string& name) const override;

    // Acceso al registry de contratos — útil para code paths que
    // necesitan el DeviceContract* además del asset (p. ej. apertura del
    // panel de mapping requiere el contrato).
    const scinodes::ContractRegistry& contracts() const { return m_contracts; }

private:
    const scinodes::ContractRegistry&                m_contracts;
    std::unordered_map<int, scinodes::DeviceAsset>   m_cache;
    std::unordered_map<std::string, scinodes::DeviceAsset> m_namedAssets;
    std::string                                      m_baseDir;
};

}  // namespace scinodes::app
