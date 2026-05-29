#pragma once
#include "../app/FileDialog.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScnSerializer.hpp"
#include "../core/UndoRedoStack.hpp"
#include <functional>
#include <imgui.h>
#include <optional>
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------
// NodeCanvas — imnodes editor wrapper for Stage 2.
//
// Owns the NodeGraph and UndoRedoStack.
//
// Keyboard shortcuts:
//   Shift+A        → Add-node popup (at cursor)
//   Delete/Backspace → Delete selected nodes and/or edges
//   Ctrl+Z         → Undo
//   Ctrl+Y / Ctrl+Shift+Z → Redo
//   Ctrl+scroll    → Zoom (0.25× – 3.0×)
// -----------------------------------------------------------------------
class NodeCanvas {
public:
    void init();
    void draw();
    void clear();
    void addNode(NodeType type);                            // records undo, adds to graph
    void addCustomNode(const std::string& customType);      // for JSON-loaded types
    void resetView();

    // ---- persistence (calls ScnSerializer) ------------------------------
    bool       saveToFile(const std::string& path);
    LoadReport loadFromFile(const std::string& path);

    // ---- read-only mode (set automatically after a load with violations) -
    void setReadOnly(bool v) { m_readOnly = v; }
    bool isReadOnly() const  { return m_readOnly; }

    // Callback fired on every DragFloat tick that changes a parameter.
    // Arguments: (nodeId, paramIndex in NodeDef::params, new value).
    // AppWindow uses this to route live edits to ScilabBridge::sendParameter.
    using ParamCallback = std::function<void(int nodeId, int paramIdx, double value)>;
    void setParamCallback(ParamCallback cb) { m_paramCallback = std::move(cb); }

    // Mark a node as "first to go non-finite" — it is painted with a red
    // title bar. 0 means no node is currently highlighted.
    void setHighlightedNode(int nodeId) { m_highlightNodeId = nodeId; }

    // Accessors used by AppWindow
    const NodeGraph& graph()       const { return m_graph; }
    const std::vector<NodeInstance>& nodes() const { return m_graph.nodes(); }
    int   nodeCount()    const { return m_graph.nodeCount(); }
    int   edgeCount()    const { return m_graph.edgeCount(); }
    const char* grammarLabel() const { return m_graph.grammarLabel(); }

private:
    void drawNode(const NodeInstance& n);
    void drawEdges();
    void handleLinkCreated();
    void handleDeletion();
    void handleUndoRedo();
    void handleZoom();
    void handleParamPanelTrigger();   // sets m_openParamPanelNodeId on double-click
    void drawAddPopup();
    void drawParamPanel();            // floating window with sliders per param
    void showErrorTooltip();

    NodeGraph     m_graph;
    UndoRedoStack m_history;

    float  m_zoom      = 1.0f;
    ImVec2 m_popupPos  = {};

    // Error tooltip
    std::string m_errorMsg;
    float       m_errorTimer = 0.0f;

    // Undo snapshot taken at the start of a parameter drag/edit.
    // Committed to history when the widget loses focus after editing.
    std::optional<GraphSnapshot> m_pendingParamBefore;

    // Persistence — per-node canvas positions (kept synced from imnodes at
    // save time, applied to imnodes after a load).
    ScnPositions m_positions;
    bool         m_applyPositionsPending = false;
    bool         m_readOnly              = false;

    ParamCallback m_paramCallback;
    int           m_highlightNodeId = 0;

    // Per-node parameter panel — opened by double-clicking a node.
    int    m_openParamPanelNodeId = 0;
    ImVec2 m_paramPanelPos        = {};

    // "Load Custom Node from JSON…" entry in the add-popup. The dialog
    // runs in a worker thread; we poll its result each frame and feed
    // the path into CustomNodeRegistry::loadFromFile.
    FileDialog  m_loadCustomDialog;
    std::string m_customLoadStatus;     // last result/error, shown briefly

    // Param-block CSV import/export (per-node), driven from the param
    // panel. Polls every frame like the custom-node loader. The node
    // id is captured at click time so a later graph mutation doesn't
    // race the dialog.
    enum class ParamCsvAction { None, Export, Import };
    FileDialog        m_paramCsvDialog;
    ParamCsvAction    m_paramCsvAction = ParamCsvAction::None;
    int               m_paramCsvNodeId = 0;
    std::string       m_paramCsvStatus;

    // Asset glTF (modelo 3D) binding para nodos NodeCategory::Device.
    // Cuando el usuario asigna un asset desde el panel de parámetros,
    // se valida contra el contrato del tipo y se cachea aquí por id.
    FileDialog                                    m_assetDialog;
    int                                           m_assetDialogNodeId = 0;
    std::unordered_map<int, scinodes::DeviceAsset> m_loadedAssets;

    // Re-carga el asset del nodo desde n.assetPath, lo valida contra
    // su contrato, y actualiza m_loadedAssets[nodeId].  No-op si no
    // hay contrato registrado para el tipo o si assetPath está vacío.
    void reloadAssetFor(int nodeId);

public:
    // Accesores usados por el Outliner (lectura de la caché + acciones
    // sobre el asset asignado a un nodo).
    const std::unordered_map<int, scinodes::DeviceAsset>&
    loadedAssets() const { return m_loadedAssets; }

    // Re-evalúa el asset del nodo (útil tras editar el .gltf en
    // disco).  Equivale a borrar la entrada y dejar que la UI la
    // re-cargue en el próximo frame.
    void reloadAsset(int nodeId) { reloadAssetFor(nodeId); }

    // Quita el asset del nodo: assetPath vacío + entrada de caché borrada.
    void detachAsset(int nodeId) {
        m_graph.setAssetPath(nodeId, "");
        m_loadedAssets.erase(nodeId);
    }

private:
    void syncPositionsFromImnodes();
    void applyPositionsToImnodes();
};
