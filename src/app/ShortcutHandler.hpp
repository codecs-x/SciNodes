#pragma once

#include "FileActions.hpp"

// -----------------------------------------------------------------------------
// ShortcutHandler — atajos de teclado globales.
//
// Esta clase concentra la asignación atajo→acción a nivel app, sin
// que AppWindow tenga que conocer cada tecla individualmente.  Los
// atajos específicos de un panel (Shift+A en NodeCanvas, Ctrl+Z para
// undo del canvas, Del para borrar selección) se quedan dentro del
// panel correspondiente porque dependen de estado canvas-local.
//
// Atajos manejados aquí:
//
//   Ctrl+N           → File → New
//   Ctrl+O           → File → Open
//   Ctrl+S           → File → Save
//   Ctrl+Shift+S     → File → Save As
//
// Dependencia inyectada (ref no-owning):
//   - FileActions&   — destino de las acciones del menú File.
//
// Pattern: Command + SRP.  La clase no tiene estado propio; cada
// frame inspecciona ImGui::GetIO() y dispatcha.
// -----------------------------------------------------------------------------
namespace scinodes::app {

class ShortcutHandler {
public:
    explicit ShortcutHandler(FileActions& files) : m_files(files) {}

    // Llamar una vez por frame, después de que ImGui haya procesado
    // el input.  No retorna nada — los efectos se aplican vía
    // FileActions.
    void poll();

private:
    FileActions& m_files;
};

}  // namespace scinodes::app
