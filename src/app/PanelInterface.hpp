#pragma once

#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// IPanel / Area / PanelRegistry
//
// Patrón Strategy aplicado a la UI: cada "panel" de SciNodes
// (NodeEditor, 3D View, Plots, Outliner, ...) implementa IPanel.  Una
// Area es un host genérico que: (1) abre un ImGui window con título
// dinámico — el del panel actual —, (2) renderiza un menu bar con un
// selector "≡" que permite cambiar el panel sin tocar el dock layout,
// y (3) delega el contenido a IPanel::drawContent().
//
// La motivación es la flexibilidad estilo Blender: cualquier área del
// layout puede mostrar cualquier panel.  El refactor elimina el
// singleton/enum hardcodeado del primer intento y queda como una
// arquitectura debugeable, extensible y testeable.
// -----------------------------------------------------------------------------
namespace scinodes::ui {

// -----------------------------------------------------------------------------
// IPanel — la interfaz Strategy.  Los paneles concretos exponen sólo el
// "qué" (contenido y metadatos); el "cómo" (Begin/End del window,
// estilo, menu bar) lo aporta Area.
// -----------------------------------------------------------------------------
class IPanel {
public:
    virtual ~IPanel() = default;

    // Identificador estable, usado en presets de workspace y en
    // serialización ("node_editor", "view3d", "plots", "outliner").
    virtual const char* typeId() const = 0;

    // Nombre legible que el Area pone como título del window y que el
    // selector "≡" lista en su popup.
    virtual const char* displayName() const = 0;

    // Render del contenido — SIN ImGui::Begin/End ni style colors
    // específicos del window; el Area se encarga.  Llamado una vez por
    // frame, sólo si el panel está asignado a una Area visible.
    virtual void drawContent() = 0;
};

// -----------------------------------------------------------------------------
// PanelRegistry — catálogo de IPanels disponibles, indexado por typeId.
// AppWindow registra todos los paneles concretos al iniciar; las Areas
// piden referencias para hacer swap.
// -----------------------------------------------------------------------------
class PanelRegistry {
public:
    // Toma ownership del panel.  Si ya hay uno con el mismo typeId,
    // el viejo se reemplaza.
    void add(std::unique_ptr<IPanel> panel);

    // Devuelve nullptr si no hay panel con ese typeId.
    IPanel* find(const char* typeId) const;

    // Lista de todos los paneles registrados, en orden de inserción.
    std::vector<IPanel*> list() const;

private:
    std::vector<std::unique_ptr<IPanel>> m_panels;
};

// -----------------------------------------------------------------------------
// Area — host genérico de IPanels.  N areas viven en AppWindow y cada
// una se dockea en una posición distinta del layout.  La Area conserva
// un id estable; su título visible cambia con el panel activo.
//
// Ciclo de vida típico de un frame:
//
//   for (Area& a : m_areas) a.draw(registry);
//   for (Area& a : m_areas) if (auto* p = a.takePendingSwap()) a.setPanel(p);
//
// La separación entre `draw` y `takePendingSwap` es para que el swap se
// aplique DESPUÉS de que todas las areas hayan dibujado, evitando que
// una area que ya pasó a otro panel se redibuje en el mismo frame.
// -----------------------------------------------------------------------------
class Area {
public:
    // `id` debe ser único en la app.  Lo usamos como sufijo `###area_N`
    // para que ImGui mantenga el dock state aunque el título visible
    // cambie por swap.
    Area(int id, IPanel* initial = nullptr);

    // Id estable de esta Area — usado por WorkspaceManager para
    // recordar qué Area está maximizada (Blender Ctrl+Space).
    int      id() const { return m_id; }

    // Setter explícito — workspaces usan esto para configurar el panel
    // inicial.  Pasar nullptr deja la Area "vacía".
    void     setPanel(IPanel* p);
    IPanel*  currentPanel() const { return m_current; }

    // Nombre completo del window ImGui (incluye `###area_N`).  Lo usan
    // los workspaces al DockBuilderDockWindow().
    const std::string& windowName() const { return m_windowName; }

    // Render del frame.  Si la Area no tiene panel asignado, no
    // abre window.  Cuando `fullViewport` es true (modo maximize),
    // la Area se renderiza sobre TODO el viewport principal — sin
    // dock, sin las otras Areas — y el llamador debe asegurarse de
    // que sólo UNA Area se dibuje en ese frame.
    void draw(PanelRegistry& registry, bool fullViewport = false);

    // Si en este frame el usuario pidió swap, devuelve el nuevo IPanel
    // y resetea el estado interno.  El llamador (AppWindow) hace el
    // setPanel.  Devuelve nullptr si no hubo swap pendiente.
    IPanel* takePendingSwap();

private:
    int         m_id;
    IPanel*     m_current     = nullptr;
    IPanel*     m_pendingSwap = nullptr;
    std::string m_windowName;

    void refreshWindowName();
    void drawTypeSelector(PanelRegistry& registry);
};

}  // namespace scinodes::ui
