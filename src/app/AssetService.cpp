#include "AssetService.hpp"

#include "../core/NodeType.hpp"   // typeName helper (no usado aquí; los callers lo pasan)

#include <fstream>

namespace scinodes::app {

AssetService::AssetService(const scinodes::ContractRegistry& contracts)
    : m_contracts(contracts) {}

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

    std::string err;
    m_cache[nodeId] = scinodes::DeviceAssetLoader::load(path, *contract, &err);
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

}  // namespace scinodes::app
