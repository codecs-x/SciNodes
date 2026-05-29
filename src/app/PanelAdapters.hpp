#pragma once

#include "PanelContext.hpp"
#include "PanelInterface.hpp"

#include "../ui/NodeCanvas.hpp"
#include "../ui/OutlinerPanel.hpp"
#include "../ui/PlotPanel.hpp"
#include "../ui/View3DPanel.hpp"

// -----------------------------------------------------------------------------
// Adaptadores IPanel para cada panel concreto.
//
// Cada adapter:
//   - guarda una ref no-owning al panel concreto que vive en AppWindow,
//   - guarda una ref a IPanelContext para resolver graph()/bridge()/
//     loadedAssets() (DIP — depende de la abstracción, no del Canvas
//     y Bridge concretos),
//   - implementa drawContent() llamando al método del panel con los
//     argumentos del contexto.
// -----------------------------------------------------------------------------
namespace scinodes::ui {

class NodeEditorPanelAdapter : public IPanel {
public:
    NodeEditorPanelAdapter(NodeCanvas& canvas) : m_canvas(canvas) {}
    const char* typeId()      const override { return "node_editor"; }
    const char* displayName() const override { return "Node Editor"; }
    void drawContent() override { m_canvas.drawContent(); }
private:
    NodeCanvas& m_canvas;
};

class View3DPanelAdapter : public IPanel {
public:
    View3DPanelAdapter(View3DPanel& v, scinodes::app::IPanelContext& ctx)
        : m_view(v), m_ctx(ctx) {}
    const char* typeId()      const override { return "view3d"; }
    const char* displayName() const override { return "3D View"; }
    void drawContent() override {
        m_view.drawContent(m_ctx.graph(), m_ctx.bridge(), m_ctx.loadedAssets());
    }
private:
    View3DPanel&                  m_view;
    scinodes::app::IPanelContext& m_ctx;
};

class PlotsPanelAdapter : public IPanel {
public:
    PlotsPanelAdapter(PlotPanel& panel, scinodes::app::IPanelContext& ctx)
        : m_panel(panel), m_ctx(ctx) {}
    const char* typeId()      const override { return "plots"; }
    const char* displayName() const override { return "Plots"; }
    void drawContent() override {
        m_panel.drawContent(m_ctx.graph(), m_ctx.bridge());
    }
private:
    PlotPanel&                    m_panel;
    scinodes::app::IPanelContext& m_ctx;
};

class OutlinerPanelAdapter : public IPanel {
public:
    OutlinerPanelAdapter(OutlinerPanel& panel, scinodes::app::IPanelContext& ctx)
        : m_panel(panel), m_ctx(ctx) {}
    const char* typeId()      const override { return "outliner"; }
    const char* displayName() const override { return "Outliner"; }
    void drawContent() override { m_panel.drawContent(m_ctx.canvas()); }
private:
    OutlinerPanel&                m_panel;
    scinodes::app::IPanelContext& m_ctx;
};

}  // namespace scinodes::ui
