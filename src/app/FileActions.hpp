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
    void requestImport();
    void requestSave();
    void requestSaveAs();
    void requestSaveAsExample();
    // Menú Archivo → Importar modelo 3D.  Abre un diálogo de .gltf/.glb,
    // y al elegir carga el asset contract-less, lo registra en el
    // catálogo del NodeGraph y en el cache by-name del AssetService.
    // No destruye trabajo — no necesita gate de "cambios sin guardar".
    void requestImportModel3D();
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
    // muestra popups de error/reporte si los hay.  Si hay cambios sin
    // guardar, gatea con el modal de confirmación.
    void openFromPath(const std::string& path);

    // Carga un .scn de la biblioteca de ejemplos.  Igual que openFromPath
    // pero marca la sesión como "template": Save subsecuente se comporta
    // como Save As para que el archivo original del ejemplo NUNCA se
    // sobreescriba accidentalmente.  El usuario que quiera modificar un
    // ejemplo debe pasar por File → Save as example explícitamente.
    void openExample(const std::string& path);

    // Importa un .scn al grafo actual (merge sin destruir).  No invoca
    // el modal de "cambios sin guardar" — un import nunca pierde
    // trabajo.  El llamador (ExamplesBrowser, requestImport) toma el
    // path y lo entrega aquí.
    void importFromPath(const std::string& path);

    // ¿Hay cambios sin guardar desde el último save/load/new?  La UI
    // usa esto para mostrar un asterisco en el título de la ventana.
    bool hasUnsavedChanges() const;

    // Pide cerrar la aplicación.  Gatea con el modal de "cambios sin
    // guardar" si corresponde.  Tras un Discard, Save+chain, o un grafo
    // limpio, `shouldQuit()` pasa a true; el caller (AppWindow) lo
    // consume cada frame y baja `m_running`.  Cancel deja shouldQuit()
    // en false.
    void requestQuit();
    bool shouldQuit() const { return m_quitGranted; }

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
        None, OpenLoad, ImportLoad, ImportModel3D, SaveAs, ExportSod,
        ExportCsvWide, ExportCsvFolder
    };

    // Acciones destructivas (pisan el grafo actual) que pueden quedar
    // pendientes hasta que el usuario resuelva el modal "Cambios sin
    // guardar".  None = no hay nada pendiente.
    enum class PendingDestructive {
        None,
        New,             // requestNew → m_canvas.clear()
        OpenDialog,      // requestOpen → abrir file dialog
        LoadPath,        // openFromPath(m_pendingLoadPath)
        Quit             // SDL_QUIT → m_quitGranted = true
    };

    void pollFileDialog();
    void doNew              ();                            // sin gate de unsaved
    void doRequestOpen      ();                            // sin gate de unsaved
    void doLoad             (const std::string& path);     // sin gate de unsaved
    void doImport           (const std::string& path);
    void doImportModel3D    (const std::string& path);
    void doSave             (const std::string& path);
    void doExportSod        (const std::string& path);
    void doExportCsvWide    (const std::string& path);
    void doExportCsvFolder  (const std::string& path);
    void renderLoadReportPopup();
    void renderLoadErrorPopup();
    void renderUnsavedChangesPopup();

    // Captura el dirtyRev del canvas y lo guarda como baseline.  Llamar
    // tras cualquier save/load/new exitoso.  Comparar dirtyRev contra
    // este baseline es la definición operacional de "tiene cambios sin
    // guardar".
    void markSaved();

    // Si hay una acción destructiva pendiente y el grafo está limpio
    // (o el usuario eligió descartar), ejecutarla.  Llamado tras
    // resolver el modal o tras un save exitoso.
    void runPendingDestructive();

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

    // Tracking de "cambios sin guardar".  m_savedRev = snapshot del
    // canvas.dirtyRevision() al último save/load/new.  Mientras el
    // dirtyRev actual coincida con m_savedRev, no hay nada que perder.
    int            m_savedRev = 0;

    // Estado del modal de confirmación.  Si m_pendingDestructive != None
    // y m_showUnsavedModal == true, el modal está visible y bloquea
    // hasta que el usuario decida (Guardar / Descartar / Cancelar).
    PendingDestructive m_pendingDestructive = PendingDestructive::None;
    std::string        m_pendingLoadPath;          // sólo para LoadPath
    bool               m_pendingLoadIsExample = false;  // LoadPath viene de Examples
    bool               m_showUnsavedModal = false;
    // Si el usuario eligió "Guardar" en el modal, encolamos la acción
    // destructiva para ejecutarse tras un save exitoso.  Esto preserva
    // el flujo "guardar y luego abrir" como una sola interacción.
    bool               m_runPendingAfterSave = false;

    // Se enciende cuando una acción Quit pendiente queda resuelta
    // (Descartar o save-and-quit).  AppWindow lo lee cada frame y
    // baja su m_running.
    bool               m_quitGranted = false;

    // Sesión cargada desde la biblioteca de ejemplos: Save subsecuente
    // se redirige a Save As para no pisar el archivo template.  Se
    // limpia al hacer un Save As explícito (=> el archivo nuevo SÍ es
    // el path actual y futuros Save lo sobreescriben).
    bool               m_currentIsExample = false;
};

}  // namespace scinodes::app
