#include "ShortcutHandler.hpp"

#include <imgui.h>

namespace scinodes::app {

void ShortcutHandler::poll() {
    const ImGuiIO& io = ImGui::GetIO();

    // Ctrl+S / Ctrl+Shift+S — la única tecla que distingue por shift.
    if (io.KeyCtrl && !io.KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) m_files.requestSaveAs();
        else             m_files.requestSave();
        return;   // un atajo por frame para evitar dobles
    }

    // El resto requiere Shift OFF para no chocar con shortcuts del
    // canvas u otras combinaciones.
    if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) m_files.requestOpen();
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) m_files.requestNew();
    }
}

}  // namespace scinodes::app
