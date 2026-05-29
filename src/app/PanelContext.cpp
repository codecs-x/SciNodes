#include "PanelContext.hpp"

#include "AssetService.hpp"
#include "../ui/NodeCanvas.hpp"

namespace scinodes::app {

const NodeGraph& PanelContext::graph() const {
    return m_canvas.graph();
}

const std::unordered_map<int, scinodes::DeviceAsset>&
PanelContext::loadedAssets() const {
    return m_canvas.loadedAssets();
}

// Fallback resolver — todo nullptr, todo el tiempo.  Lo usamos cuando el
// NodeCanvas aún no tiene AssetService cableado (p. ej. tests headless
// o un panel que se dibuja antes de la inicialización completa).
namespace {
struct NullSceneAssetResolver : public scinodes::ISceneAssetResolver {
    const scinodes::DeviceAsset* resolveByName(const std::string&) const override {
        return nullptr;
    }
};
}  // anon

const scinodes::ISceneAssetResolver& PanelContext::sceneResolver() const {
    static const NullSceneAssetResolver kNull;
    if (auto* svc = m_canvas.assetService()) return *svc;
    return kNull;
}

}  // namespace scinodes::app
