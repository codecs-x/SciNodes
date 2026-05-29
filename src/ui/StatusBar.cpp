#include "StatusBar.hpp"
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
                          const char* lastError) {
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
        if (ImGui::Button("  \xe2\x96\xb6  Run  "))   action = SimAction::Run;
        popButtonStyle();
        if (!canRun) ImGui::EndDisabled();
    } else if (state == SimState::Simulating) {
        pushButtonStyle(IM_COL32(180, 130,  20, 255),
                        IM_COL32(210, 160,  40, 255),
                        IM_COL32(140, 100,  10, 255));
        if (ImGui::Button("  \xe2\x8f\xb8  Pause  "))  action = SimAction::Pause;   // ⏸
        popButtonStyle();
        ImGui::SameLine();
        pushButtonStyle(IM_COL32(160,  55,  35, 255),
                        IM_COL32(200,  75,  50, 255),
                        IM_COL32(120,  40,  25, 255));
        if (ImGui::Button("  \xe2\x96\xa0  Stop  "))   action = SimAction::Stop;
        popButtonStyle();
    } else if (state == SimState::Paused) {
        pushButtonStyle(IM_COL32( 36, 120,  52, 255),
                        IM_COL32( 50, 160,  70, 255),
                        IM_COL32( 30,  90,  42, 255));
        if (ImGui::Button("  \xe2\x96\xb6  Resume  ")) action = SimAction::Resume;
        popButtonStyle();
        ImGui::SameLine();
        pushButtonStyle(IM_COL32(160,  55,  35, 255),
                        IM_COL32(200,  75,  50, 255),
                        IM_COL32(120,  40,  25, 255));
        if (ImGui::Button("  \xe2\x96\xa0  Stop  "))   action = SimAction::Stop;
        popButtonStyle();
    }

    ImGui::SameLine();
    pushButtonStyle(IM_COL32( 60,  60,  60, 255),
                    IM_COL32( 85,  85,  85, 255),
                    IM_COL32( 42,  42,  42, 255));
    if (ImGui::Button("  \xe2\x86\xba  Reset  "))    action = SimAction::Reset;
    popButtonStyle();

    // ---- Separator -------------------------------------------------------
    ImGui::SameLine(); ImGui::TextDisabled(" | "); ImGui::SameLine();

    // ---- State badge ------------------------------------------------------
    const char* badge = grammarLabel;
    ImU32 badgeCol    = IM_COL32(60, 60, 60, 255);
    switch (state) {
        case SimState::Simulating: badge = "Simulating";
            badgeCol = IM_COL32(180, 130,  20, 255); break;
        case SimState::Paused:     badge = "Paused";
            badgeCol = IM_COL32(120, 120, 140, 255); break;
        case SimState::Error:      badge = "Error";
            badgeCol = IM_COL32(200,  50,  50, 255); break;
        case SimState::Idle:
            if (std::string(grammarLabel) == "Editing")
                badgeCol = IM_COL32( 50, 100, 200, 255);
            else if (std::string(grammarLabel) == "Valid")
                badgeCol = IM_COL32( 40, 140,  60, 255);
            break;
    }
    pushButtonStyle(badgeCol, badgeCol, badgeCol);
    ImGui::Button(badge);
    popButtonStyle();

    ImGui::SameLine(); ImGui::TextDisabled(" | "); ImGui::SameLine();

    // ---- Graph stats + sim time -----------------------------------------
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Nodes: %d   Edges: %d   t = %.3f s",
                  nodeCount, edgeCount, simTime);
    ImGui::TextUnformatted(buf);

    // ---- Error message (if any) -----------------------------------------
    if (state == SimState::Error && lastError && *lastError) {
        ImGui::SameLine(); ImGui::TextDisabled(" | "); ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
        ImGui::TextUnformatted(lastError);
        ImGui::PopStyleColor();
    }

    // ---- FPS (right-aligned) --------------------------------------------
    ImGuiIO& io = ImGui::GetIO();
    std::snprintf(buf, sizeof(buf), "%.0f FPS", io.Framerate);
    float fpsW = ImGui::CalcTextSize(buf).x + 12.f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - fpsW);
    ImGui::TextDisabled("%s", buf);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    return action;
}
