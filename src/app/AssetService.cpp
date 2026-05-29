#include "AssetService.hpp"

#include "../core/NodeType.hpp"   // typeName helper (no usado aquí; los callers lo pasan)

#include <filesystem>
#include <fstream>

namespace scinodes::app {

namespace fs = std::filesystem;

AssetService::AssetService(const scinodes::ContractRegistry& contracts)
    : m_contracts(contracts) {}

std::string AssetService::resolveAssetPath(const std::string& assetPath) const {
    if (assetPath.empty()) return assetPath;
    fs::path p(assetPath);
    // Absoluta y existe — la usamos tal cual.
    if (p.is_absolute() && fs::exists(p)) return assetPath;
    // Relativa al cwd actual.
    if (fs::exists(p)) return fs::absolute(p).string();
    // Relativa al directorio del .scn (m_baseDir), y a sus ancestros.
    if (!m_baseDir.empty()) {
        fs::path probe(m_baseDir);
        for (int i = 0; i < 6; ++i) {
            fs::path cand = probe / p;
            if (fs::exists(cand)) return fs::absolute(cand).string();
            if (!probe.has_parent_path() || probe.parent_path() == probe) break;
            probe = probe.parent_path();
        }
    }
    return assetPath;  // no resolvió — el loader reportará el error
}

bool AssetService::reload(int                nodeId,
                          const std::string& typeName,
                          const std::string& path) {
    if (path.empty()) {
        detach(nodeId);
        return false;
    }

    const auto* contract = m_contracts.find(typeName);
    if (!contract) {
        // Sin contrato no podemos validar; igual borramos la entrada
        // cacheada para que la UI muestre "(sin contrato registrado)".
        m_cache.erase(nodeId);
        return false;
    }

    const std::string resolved = resolveAssetPath(path);
    std::string err;
    m_cache[nodeId] = scinodes::DeviceAssetLoader::load(resolved, *contract, &err);
    // err se ignora — el asset.missing ya cuenta la historia para la UI.
    return true;
}

void AssetService::detach(int nodeId) {
    m_cache.erase(nodeId);
}

const scinodes::DeviceAsset* AssetService::find(int nodeId) const {
    auto it = m_cache.find(nodeId);
    return (it == m_cache.end()) ? nullptr : &it->second;
}

std::string AssetService::sidecarPathFor(const std::string& assetPath) {
    return scinodes::AssetMapping::sidecarPathFor(assetPath);
}

bool AssetService::sidecarExists(const std::string& assetPath) {
    std::ifstream f(sidecarPathFor(assetPath));
    return f.good();
}

// ---- catálogo by-name --------------------------------------------------
void AssetService::installNamedAsset(const std::string& name,
                                     scinodes::DeviceAsset asset) {
    m_namedAssets[name] = std::move(asset);
}

void AssetService::detachNamed(const std::string& name) {
    m_namedAssets.erase(name);
}

bool AssetService::hasNamed(const std::string& name) const {
    return m_namedAssets.count(name) > 0;
}

const scinodes::DeviceAsset*
AssetService::resolveByName(const std::string& name) const {
    auto it = m_namedAssets.find(name);
    return (it == m_namedAssets.end()) ? nullptr : &it->second;
}

}  // namespace scinodes::app
