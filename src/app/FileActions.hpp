#pragma once

#include "FileDialog.hpp"
#include "SimController.hpp"
#include "../core/ScilabBridge.hpp"
#include "../core/ScnSerializer.hpp"
#include "../ui/NodeCanvas.hpp"

#include <string>

// -----------------------------------------------------------------------------
// FileActions — Use Case que orquesta las operaciones de archivo
// (New / Open / Save / Save As / Export SOD).
//
// Saca de AppWindow:
//   - El FileDialog asíncrono y su drain por frame.
//   - El enum PendingAction y el manejo del ciclo abrir-elegir-actuar.
//   - El currentPath (camino del .scn actual).
//   - Los dos popups modales (LoadReport / LoadError).
//   - El toast del .sod export con su fade timer.
//   - openFromCli, usado al arrancar la app con un .scn en argv.
//
// Pattern: Use Case (Martin Clean Architecture Cap 16).  Dependencias
// inyectadas por referencia (no-owning):
//
//   - NodeCanvas&     — destino de loadFromFile / saveToFile.
//   - ScilabBridge&   — origen de exportSod / takeLastExportResult.
//   - SimController&  — se detiene la simulación cuando se abre o
//                       reemplaza un grafo (similar al menú File→New).
// -----------------------------------------------------------------------------
namespace scinodes::app {

class FileActions {
public:
    FileActions(NodeCanvas&    canvas,
                ScilabBridge&  bridge,
                SimController& sim)
        : m_canvas(canvas), m_bridge(bridge), m_sim(sim) {}

    // ---- Menu-bar entry points (called from "File" menu items) -----------
    void requestNew();
    void requestOpen();
    void requestSave();
    void requestSaveAs();
    void requestExportSod();
    // Exporta todos los sinks del top-level graph a CSV.  Wide = un solo
    // archivo con time + columna por canal.  Folder = un CSV por sink en
    // una carpeta elegida por el usuario.
    void requestExportCsvWide();
    void requestExportCsvFolder();

    // ---- CLI bootstrap ----------------------------------------------------
    // Carga un .scn directo (sin pasar por el diálogo).  Usado por
    // `./SciNodes path/to/graph.scn`.  Sin popups; si falla, escribe a
    // stderr.
    void openFromCli(const std::string& path);

    // Carga un .scn iniciada desde dentro de la UI (Examples browser,
    // etc.).  Mismo flujo que el diálogo File→Open: detiene la simulación,
    // muestra popups de error/reporte si los hay.
    void openFromPath(const std::string& path) { doLoad(path); }

    // ---- Per-frame work ---------------------------------------------------
    // Llamar al final de renderUI: drena el FileDialog si el usuario
    // eligió un archivo, dispara load/save/export según corresponda,
    // y muestra los popups modales pendientes.
    void update();

    // ---- Toast del .sod export -------------------------------------------
    // Drena el resultado pendiente del bridge y renderiza un toast
    // verde/rojo durante ~5 s.  Se invoca desde dentro del menu bar.
    void drawExportToast();

    // Read-only accessor para el título de la ventana, status bar, etc.
    const std::string& currentPath() const { return m_currentPath; }

private:
    enum class PendingAction {
        None, OpenLoad, SaveAs, ExportSod, ExportCsvWide, ExportCsvFolder
    };

    void pollFileDialog();
    void doLoad             (const std::string& path);
    void doSave             (const std::string& path);
    void doExportSod        (const std::string& path);
    void doExportCsvWide    (const std::string& path);
    void doExportCsvFolder  (const std::string& path);
    void renderLoadReportPopup();
    void renderLoadErrorPopup();

    NodeCanvas&    m_canvas;
    ScilabBridge&  m_bridge;
    SimController& m_sim;

    FileDialog     m_fileDialog;
    PendingAction  m_pendingAction = PendingAction::None;
    std::string    m_currentPath;        // último .scn abierto/guardado

    LoadReport     m_lastReport;
    bool           m_showReportPopup = false;
    bool           m_showErrorPopup  = false;

    std::string    m_exportStatus;
    float          m_exportStatusTimer = 0.0f;
};

}  // namespace scinodes::app
