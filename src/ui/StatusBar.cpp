#include "StatusBar.hpp"
#include "../core/I18n.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace {
// Coloured button helper — pushes 3 colour styles, lets caller call
// Button(), then call popButtonStyle() to restore.
void pushButtonStyle(ImU32 base, ImU32 hov, ImU32 act) {
    ImGui::PushStyleColor(ImGuiCol_Button,        base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
}
void popButtonStyle() { ImGui::PopStyleColor(3); }
} // namespace

SimAction StatusBar::draw(int nodeCount, int edgeCount,
                          const char* grammarLabel,
                          SimState state,
                          bool grammarValid,
                          float simTime,
                          const char* lastError,
                          bool stale) {
    SimAction action = SimAction::None;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    float barH        = ImGui::GetFrameHeight() + 8.0f;
    ImVec2 barPos     = { vp->Pos.x, vp->Pos.y + vp->Size.y - barH };
    ImVec2 barSize    = { vp->Size.x, barH };

    ImGui::SetNextWindowPos(barPos);
    ImGui::SetNextWindowSize(barSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize      |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {8, 4});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    {6, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(22, 22, 22, 255));
    ImGui::Begin("##StatusBar", nullptr, flags);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f);

    // ---- Run / Pause / Resume / Stop --------------------------------------
    if (state == SimState::Idle || state == SimState::Error) {
        const bool canRun = grammarValid && state != SimState::Error;
        if (!canRun) ImGui::BeginDisabled();
        pushButtonStyle(IM_COL32( 36, 120,  52, 255),
                        IM_COL32( 50, 160,  70, 255),
                        IM_COL32( 30,  90,  42, 255));
        if (ImGui::Button(scinodes::tr("statusbar.run").c_str()))
            action = SimAction::Run;
        popButtonStyle();
        if (!canRun) ImGui::EndDisabled();
    } else if (state == SimState::Simulating) {
        pushButtonStyle(IM_COL32(180, 130,  20, 255),
                        IM_COL32(210, 160,  40, 255),
                        IM_COL32(140, 100,  10, 255));
        if (ImGui::Button(scinodes::tr("statusbar.pause").c_str()))
            action = SimAction::Pause;
        popButtonStyle();
        ImGui::SameLine();
        pushButtonStyle(IM_COL32(160,  55,  35, 255),
                        IM_COL32(200,  75,  50, 255),
                        IM_COL32(120,  40,  25, 255));
        if (ImGui::Button(scinodes::tr("statusbar.stop").c_str()))
            action = SimAction::Stop;
        popButtonStyle();
    } else if (state == SimState::Paused) {
        pushButtonStyle(IM_COL32( 36, 120,  52, 255),
                        IM_COL32( 50, 160,  70, 255),
                        IM_COL32( 30,  90,  42, 255));
        if (ImGui::Button(scinodes::tr("statusbar.resume").c_str()))
            action = SimAction::Resume;
        popButtonStyle();
        ImGui::SameLine();
        pushButtonStyle(IM_COL32(160,  55,  35, 255),
                        IM_COL32(200,  75,  50, 255),
                        IM_COL32(120,  40,  25, 255));
        if (ImGui::Button(scinodes::tr("statusbar.stop").c_str()))
            action = SimAction::Stop;
        popButtonStyle();
    }

    ImGui::SameLine();
    pushButtonStyle(IM_COL32( 60,  60,  60, 255),
                    IM_COL32( 85,  85,  85, 255),
                    IM_COL32( 42,  42,  42, 255));
    if (ImGui::Button(scinodes::tr("statusbar.reset").c_str()))
        action = SimAction::Reset;
    popButtonStyle();

    // ---- Separator -------------------------------------------------------
    ImGui::SameLine(); ImGui::TextDisabled(" | "); ImGui::SameLine();

    // ---- State badge ------------------------------------------------------
    // El grammarLabel viene crudo del NodeGraph ("Editing"/"Valid"/...);
    // lo mapeamos a una clave de i18n para que cambie con el idioma.
    std::string badgeStr;
    ImU32 badgeCol = IM_COL32(60, 60, 60, 255);
    switch (state) {
        case SimState::Simulating:
            badgeStr = scinodes::tr("statusbar.state.simulating");
            badgeCol = IM_COL32(180, 130,  20, 255); break;
        case SimState::Paused:
            badgeStr = scinodes::tr("statusbar.state.paused");
            badgeCol = IM_COL32(120, 120, 140, 255); break;
        case SimState::Error:
            badgeStr = scinodes::tr("statusbar.state.error");
            badgeCol = IM_COL32(200,  50,  50, 255); break;
        case SimState::Idle: {
            const std::string g = grammarLabel;
            if      (g == "Editing") { badgeStr = scinodes::tr("statusbar.state.editing");
                                        badgeCol = IM_COL32( 50, 100, 200, 255); }
            else if (g == "Valid")   { badgeStr = scinodes::tr("statusbar.state.valid");
                                        badgeCol = IM_COL32( 40, 140,  60, 255); }
            else if (g == "Invalid") { badgeStr = scinodes::tr("statusbar.state.invalid"); }
            else                     { badgeStr = g; }  // unknown → raw
            break;
        }
    }
    pushButtonStyle(badgeCol, badgeCol, badgeCol);
    ImGui::Button(badgeStr.c_str());
    popButtonStyle();

    // (El badge "stale" se removió: el flujo es ahora Pausa → editar →
    // Reanudar, donde Reanudar hace hot-reload con seed del estado
    // capturado.  Si la sim sigue corriendo y el usuario edita, la
    // edición no se aplica hasta que pulse Pausa + Reanudar — eso es
    // intencional y no requiere alarma visual permanente.)
    (void)stale;

    ImGui::SameLine(); ImGui::TextDisabled(" | "); ImGui::SameLine();

    // ---- Graph stats + sim time -----------------------------------------
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: %d   %s: %d   t = %.3f s",
                  scinodes::tr("statusbar.nodes").c_str(), nodeCount,
                  scinodes::tr("statusbar.edges").c_str(), edgeCount,
                  simTime);
    ImGui::TextUnformatted(buf);

    // ---- Error message (if any) -----------------------------------------
    if (state == SimState::Error && lastError && *lastError) {
        ImGui::SameLine(); ImGui::TextDisabled(" | "); ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
        ImGui::TextUnformatted(lastError);
        ImGui::PopStyleColor();
    }

    // ---- Frame stats + FPS (right-aligned) ------------------------------
    ImGuiIO& io = ImGui::GetIO();
    if (m_profiling) {
        std::snprintf(buf, sizeof(buf),
            "in %.2f  up %.2f  rd %.2f  pr %.2f ms  |  %.0f FPS",
            m_stats.inputMs, m_stats.updateMs,
            m_stats.renderMs, m_stats.presentMs,
            io.Framerate);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f FPS", io.Framerate);
    }
    float statsW = ImGui::CalcTextSize(buf).x + 12.f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - statsW);
    ImGui::TextDisabled("%s", buf);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    return action;
}
