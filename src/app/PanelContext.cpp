#include "PanelContext.hpp"

#include "../ui/NodeCanvas.hpp"

namespace scinodes::app {

const NodeGraph& PanelContext::graph() const {
    return m_canvas.graph();
}

const std::unordered_map<int, scinodes::DeviceAsset>&
PanelContext::loadedAssets() const {
    return m_canvas.loadedAssets();
}

}  // namespace scinodes::app
